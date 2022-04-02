//
// Created by wyz on 2022/2/25.
//
#include "core/BlockVolumeManager.hpp"
#include "core/GPUResource.hpp"
#include "core/VolumeBlockTree.hpp"
#include "utils/Timer.hpp"
#include "plugin/PluginLoader.hpp"
#include "common/Logger.hpp"
#include "common/Parrallel.hpp"
#include <SDL.h>
#include "algorithm/GeometryHelper.hpp"
using namespace mrayns;

struct WindowContext{
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    SDL_Texture* frame{nullptr};
    int width,height;
};
WindowContext window_context;
WindowContext* wc = &window_context;

bool InitWindowContext(int w,int h){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0){
        LOG_ERROR("sdl init failed");
        return false;
    }
    wc->width = w;
    wc->height = h;
    wc->window = SDL_CreateWindow("LargeVolumeVis",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,w,h,0);
    if(!wc->window){
        LOG_ERROR("create sdl window failed");
        return false;
    }
    wc->renderer = SDL_CreateRenderer(wc->window,-1,SDL_RENDERER_ACCELERATED);
    if(!wc->renderer){
        LOG_ERROR("create sdl renderer failed");
        return false;
    }
    //sdl ABGR8888 packed from high to low bit so it equal to RGBA32
    //vulkan RGBA888 is from low to high so it equal to sdl RGBA32
    wc->frame = SDL_CreateTexture(wc->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,w,h);
    if(!wc->frame){
        LOG_ERROR("create sdl texture failed");
        return false;
    }
    return true;
}

void Run(){
    PluginLoader::LoadPlugins("./plugins");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path="E:/MouseNeuronData/mouse_file_config.json";
    p->open(h264_file_path);
    assert(p.get());

    auto& host_node = HostNode::getInstance();
    host_node.setGPUNum(1);

    p->setHostNode(&host_node);

    auto volume = p->getVolume();

    VolumeBlockTree volume_block_tree;
    volume_block_tree.buildTree(volume);

    auto& block_volume_manager = BlockVolumeManager::getInstance();

    block_volume_manager.setProvider(std::move(p));

    block_volume_manager.init();

    GPUResource gpu_resource(0);
    GPUResource::ResourceDesc desc{};
    desc.type = mrayns::GPUResource::Texture;
    desc.width = 1024;
    desc.height = 1024;
    desc.depth = 1024;
    desc.pitch = 1024;
    desc.block_length = volume.getBlockLength();
    for(int i = 0;i < 8; i++)
        gpu_resource.createGPUResource(desc);

    auto volume_renderer = RendererCaster<Renderer::VOLUME>::GetPtr(gpu_resource.getRenderer(Renderer::VOLUME));

    volume_renderer->setVolume(volume);

    const int frame_w = 1280;
    const int frame_h = 720;
    if(!InitWindowContext(frame_w,frame_h)){
        LOG_ERROR("init window context failed");
        throw std::runtime_error("Init window context failed");
    }

    CameraExt camera{};
    camera.position = {5.92f,5.92f,7.9f};
    camera.target = {5.92f,5.92f,7.f};
    camera.front = {0.f,0.f,-1.f};
    camera.up = {0.f,1.f,0.f};
    camera.near_z = 0.001f;
    camera.far_z = 1.f;
    camera.width = frame_w;
    camera.height = frame_h;
    camera.aspect = static_cast<float>(frame_w) / static_cast<float>(frame_h);
    camera.yaw = -90.f;
    camera.pitch = 0.f;


    int total_missed_count = 0;

    auto volume_render = [&]()->const Image&{


      START_TIMER
        VolumeRendererCamera renderer_camera{camera};
      //1. get current camera view and projection matrix
      auto view_matrix = GeometryHelper::ExtractViewMatrixFromCamera(renderer_camera);
      auto proj_matrix = GeometryHelper::ExtractProjMatrixFromCamera(renderer_camera);
      auto vp = proj_matrix * view_matrix;
      FrustumExt view_frustum{};
      GeometryHelper::ExtractViewFrustumPlanesFromMatrix(vp,view_frustum);


      //2. compute intersect blocks with current camera view frustum
      //需要得到不同lod的相交块
      auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum,renderer_camera.lod_dist,renderer_camera.position);
      int intersect_block_count = intersect_blocks.size();
      LOG_INFO("volume render intersect block count: {}",intersect_block_count);
//      for(auto& b:intersect_blocks){
//          LOG_INFO("intersect block {} {} {} {}",b.x,b.y,b.z,b.w);
//      }
      //3.1 assign task to different GPUResource
      //todo 在这里可以根据GPUNode的数量分割渲染任务 交给不同的GPU
      //分割并不是更高效利用GPU 而是用更多的GPU资源提升绘制速度 所以应该只在总的负载很小时开启
      //体绘制并不是很适合
      //分割任务算法可以根据不同渲染器的类别应用
      //暂时不考虑多个GPU数量
      //3.2 compute cached blocks and missed blocks
      std::vector<Volume::BlockIndex> missed_blocks;
      std::vector<Renderer::PageTableItem> cur_renderer_page_table;
      auto& page_table = gpu_resource.getPageTable();
      page_table.acquireLock();
      auto query_ret = page_table.queriesAndLockExt(intersect_blocks);
      for(const auto& ret:query_ret){
          if(ret.cached){
              cur_renderer_page_table.emplace_back(ret.entry,ret.value);
          }
          else{
              missed_blocks.emplace_back(ret.value);
          }
      }
      LOG_INFO("first time missed blocks count {}",missed_blocks.size());
      //此时获取保证其之后不会被上传写入 一定需要此处上传
      auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks);//不一定等同于missed_blocks
      page_table.acquireRelease();
      LOG_INFO("PageTable acquire release");
      std::unordered_map<Volume::BlockIndex,PageTable::EntryItem> block_entries;//真正需要上传的数据块
      std::mutex block_entries_mtx;
      missed_blocks.clear();//重新生成
      for(const auto& entry:missed_block_entries){
          if(!entry.cached){
              block_entries[entry.value] = entry.entry;
              missed_blocks.emplace_back(entry.value);
          }
          cur_renderer_page_table.emplace_back(entry.entry,entry.value);
      }
      LOG_INFO("volume render missed block count: {}",missed_blocks.size());
      total_missed_count += missed_blocks.size();
      LOG_INFO("total missed count since start render: {}",total_missed_count);
      //4 get block and upload

      auto thread_id = std::this_thread::get_id();
      auto tid = std::hash<decltype(thread_id)>()(thread_id);
      auto task = [&](int thread_idx,Volume::BlockIndex block_index){
          //单机单线程模式下需要换一种写法
          auto p = block_volume_manager.getVolumeBlockAndLock(block_index);
          std::lock_guard<std::mutex> lk(block_entries_mtx);
          auto entry = block_entries[block_index];
          GPUResource::ResourceDesc desc;
          desc.id = tid;
          desc.type = GPUResource::Texture;
          desc.width = volume.getBlockLength();
          desc.pitch = volume.getBlockLength();
          desc.height = volume.getBlockLength();
          desc.depth = volume.getBlockLength();
          desc.size = volume.getBlockSize();
          GPUResource::ResourceExtent extent{
              volume.getBlockLength(),
              volume.getBlockLength(),
              volume.getBlockLength()
          };
          gpu_resource.uploadResource(desc,entry,extent,p,volume.getBlockSize(),false);
          auto ret = block_volume_manager.unlock(p);
          assert(ret);
      };
      //4.1 get volume block and upload to GPUResource
      parallel_foreach(missed_blocks,task,missed_blocks.size());

      gpu_resource.flush(tid);
      //4.2 release write to read lock
      for(const auto& block:missed_blocks){
          page_table.update(block);
      }

      volume_renderer->updatePageTable(cur_renderer_page_table);

      //如果一次性获取的数据块数量超过内存的最大储量怎么办
      STOP_TIMER("volume render prepare")

      //5 render and merge result
      //5.1 render
      {
          START_TIMER
          volume_renderer->render(renderer_camera);
      //5.2 release read lock for page table


          for (const auto &block : intersect_blocks)
          {
              page_table.release(block);
          }
          STOP_TIMER("release page table")
      }

      const auto& ret = volume_renderer->getFrameBuffers().getColors();

      /**
       * 在这里如果是切分任务渲染的话 需要对渲染结果进行融合拼接
       */
      {

      }

      return ret;
    };
    bool exit = false;
    uint32_t delta_t = 0;
    uint32_t last_t = 0;
    float camera_move_sense = 0.05f;
    Vector3f world_up = {0.f,1.f,0.f};
    auto process_input = [&](bool& exit,uint32_t delta_t){
      static SDL_Event event;
      while(SDL_PollEvent(&event)){
          switch (event.type)
          {
          case SDL_QUIT:{
              exit = true;
              break;
          }
          case SDL_DROPFILE:{

              break;
          }
          case SDL_KEYDOWN:{



              break;
          }

          case SDL_MOUSEMOTION:{
              if (event.motion.state & SDL_BUTTON_LMASK)
              {
                  float x_offset = static_cast<float>(event.motion.xrel) * camera_move_sense;
                  float y_offset = static_cast<float>(event.motion.yrel) * camera_move_sense;

                  camera.yaw += x_offset;
                  camera.pitch -= y_offset;

                  if (camera.pitch > 89.f)
                  {
                      camera.pitch = 89.f;
                  }
                  else if (camera.pitch < -89.f)
                  {
                      camera.pitch = -89.f;
                  }

                  camera.front.x = std::cos(camera.pitch * M_PI / 180.f) * std::cos(camera.yaw * M_PI / 180.f);
                  camera.front.y = std::sin(camera.pitch * M_PI / 180.f);
                  camera.front.z = std::cos(camera.pitch * M_PI / 180.f) * std::sin(camera.yaw * M_PI / 180.f);
                  camera.front = normalize(camera.front);
                  camera.right = normalize(cross(camera.front, world_up));
                  camera.up = normalize(cross(camera.right, camera.front));
                  camera.target = camera.position + camera.front;
              }


              break;
          }
          case SDL_MOUSEWHEEL:{


              break;
          }
          }
      }
    };
    auto sdl_draw = [&](const Image& pixels){
      static SDL_Rect rect{0,0,frame_w,frame_h};
      SDL_UpdateTexture(wc->frame, nullptr,pixels.data(),pixels.pitch());
      SDL_RenderClear(wc->renderer);
      SDL_RenderCopy(wc->renderer,wc->frame, nullptr,&rect);
      SDL_RenderPresent(wc->renderer);
    };
    while(!exit){
        last_t = SDL_GetTicks();

        process_input(exit,delta_t);
        START_TIMER
        const auto& colors = volume_render();
        STOP_TIMER("volume render")


        sdl_draw(colors);


        delta_t = SDL_GetTicks() - last_t;
        std::cout<<"delta_t: "<<delta_t<<std::endl;
    }
    LOG_INFO("exit main render loop");
}

int main(int argc,char** argv){
    SET_LOG_LEVEL_DEBUG
    try{
        Run();
    }
    catch (const std::exception& err)
    {
        LOG_ERROR("{}, exit program!",err.what());
    }
    return 0;
}
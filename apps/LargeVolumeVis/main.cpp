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
#include "algorithm/ColorMapping.hpp"
#include "algorithm/RenderHelper.hpp"
#include "algorithm/VolumeHelper.hpp"
#include <set>
#include <unordered_set>
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
void SDLDraw(const Image &pixels)
{
    static SDL_Rect rect{0, 0, wc->width, wc->height};
    SDL_UpdateTexture(wc->frame, nullptr, pixels.data(), pixels.pitch());
    SDL_RenderClear(wc->renderer);
    SDL_RenderCopy(wc->renderer, wc->frame, nullptr, &rect);
    SDL_RenderCopyEx(wc->renderer, wc->frame, nullptr, &rect, 0.0, nullptr, SDL_FLIP_VERTICAL);
    SDL_RenderPresent(wc->renderer);
}
void RunRenderLoop(bool async){
    PluginLoader::LoadPlugins("C:/Users/wyz/projects/MouseBrainVisualizeProject/bin");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path="E:/MouseNeuronData/mouse_file_config0.json";
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
    for(int i = 0;i < 10; i++)
        gpu_resource.createGPUResource(desc);

    auto volume_renderer = RendererCaster<Renderer::VOLUME>::GetPtr(gpu_resource.getRenderer(Renderer::VOLUME));

    volume_renderer->setVolume(volume);

    const int frame_w = 960;
    const int frame_h = 480;
    if(!InitWindowContext(frame_w,frame_h)){
        LOG_ERROR("init window context failed");
        throw std::runtime_error("Init window context failed");
    }

    CameraExt camera{};
    camera.position = {5.3f,5.53f,8.6f};
    camera.target = {5.3f,5.53f,6.1f};
    camera.front = {0.f,0.f,-1.f};
    camera.up = {0.f,1.f,0.f};
    camera.right = {1.f,0.f,0.f};
    camera.near_z = 0.001f;
    camera.far_z = 6.f;
    camera.width = frame_w;
    camera.height = frame_h;
    camera.aspect = static_cast<float>(frame_w) / static_cast<float>(frame_h);
    camera.yaw = -90.f;
    camera.pitch = 0.f;
    camera.fov = 40.f;

    TransferFunctionExt1D transferFunctionExt1D{};
    transferFunctionExt1D.points.emplace_back(0.25f,Vector4f{0.f,1.f,0.5f,0.f});
    transferFunctionExt1D.points.emplace_back(0.6f,Vector4f{1.f,0.5f,0.f,1.f});
    ComputeTransferFunction1DExt(transferFunctionExt1D);
    volume_renderer->setTransferFunction(transferFunctionExt1D);

    std::function<const Image&()> volume_render;

    if(async){
        volume_render = [&]()->const Image&{
            START_TIMER
            VolumeRendererCamera renderer_camera{camera};
            renderer_camera.raycasting_step = volume.getVoxel() * 0.5f;
            renderer_camera.raycasting_max_dist = camera.far_z;
            LOG_INFO("render camera position: {} {} {}", camera.position.x, camera.position.y, camera.position.z);

            // 1. get current camera view and projection matrix
            auto view_matrix = GeometryHelper::ExtractViewMatrixFromCamera(renderer_camera);
            auto proj_matrix = GeometryHelper::ExtractProjMatrixFromCamera(renderer_camera);
            auto vp = proj_matrix * view_matrix;
            FrustumExt view_frustum{};
            GeometryHelper::ExtractViewFrustumPlanesFromMatrix(vp, view_frustum);
            RenderHelper::GetDefaultLodDist(volume, renderer_camera.lod_dist.lod_dist, volume.getMaxLod());

            // 2. compute intersect blocks with current camera view frustum
            //需要得到不同lod的相交块
            auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum, renderer_camera.lod_dist,
                                                                            renderer_camera.position);
            int intersect_block_count = intersect_blocks.size();
            //todo check intersect_block_count not large than gpu texture memory limit
            LOG_INFO("volume render intersect block count: {}", intersect_block_count);
            for (auto &b : intersect_blocks)
            {
                LOG_INFO("intersect block {} {} {} {}", b.x, b.y, b.z, b.w);
            }

            // 3.2 compute cached blocks and missed blocks
            std::vector<Volume::BlockIndex> missed_blocks;
            std::vector<Renderer::PageTableItem> cur_renderer_page_table;
            auto &page_table = gpu_resource.getPageTable();
            page_table.acquireLock();
            auto query_ret = page_table.queriesAndLockExt(intersect_blocks);
            for (const auto &ret : query_ret)
            {
                if (ret.cached)
                {
                    cur_renderer_page_table.emplace_back(ret.entry, ret.value);
                }
                else
                {
                    missed_blocks.emplace_back(ret.value);
                }
            }
            LOG_INFO("first time missed blocks count {}", missed_blocks.size());

            //异步加载数据块 先查询缺失块是否已经加载好
            std::unordered_map<Volume::BlockIndex, void *> missed_block_buffer;
            std::vector<Volume::BlockIndex> copy_missed_blocks;
            copy_missed_blocks.swap(missed_blocks);
            for (auto &block : copy_missed_blocks)
            {
                START_TIMER
                auto p = block_volume_manager.getVolumeBlock(block, false);
                STOP_TIMER("get volume block")
                missed_block_buffer[block] = p;
                if (p && block_volume_manager.lock(p))
                {
                    //              block_volume_manager.lock(p);//check return
                    missed_blocks.emplace_back(block);
                }
                else
                {
                    missed_block_buffer[block] = nullptr;
                }
            }

            //此时获取保证其之后不会被上传写入 一定需要此处上传
            auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks); //不一定等同于missed_blocks
            page_table.acquireRelease();
            LOG_INFO("PageTable acquire release");

            std::unordered_map<Volume::BlockIndex, PageTable::EntryItem> block_entries; //真正需要上传的数据块
            std::mutex block_entries_mtx;
            missed_blocks.clear(); //重新生成
            for (const auto &entry : missed_block_entries)
            {
                if (!entry.cached)
                {
                    block_entries[entry.value] = entry.entry;
                    missed_blocks.emplace_back(entry.value);
                }
                else
                {
                    block_volume_manager.unlock(missed_block_buffer[entry.value]);
                }
                cur_renderer_page_table.emplace_back(entry.entry, entry.value);
            }
            LOG_INFO("volume render missed block count: {}", missed_blocks.size());

            // 4 get block and upload

            auto thread_id = std::this_thread::get_id();
            auto tid = std::hash<decltype(thread_id)>()(thread_id);
            auto task = [&](int thread_idx, Volume::BlockIndex block_index) {
                auto p = missed_block_buffer[block_index];
                assert(p);
                if (!p)
                    return;

                auto entry = block_entries[block_index];
                GPUResource::ResourceDesc desc;
                desc.id = tid;
                desc.type = GPUResource::Texture;
                desc.width = volume.getBlockLength();
                desc.pitch = volume.getBlockLength();
                desc.height = volume.getBlockLength();
                desc.depth = volume.getBlockLength();
                desc.size = volume.getBlockSize();
                GPUResource::ResourceExtent extent{volume.getBlockLength(), volume.getBlockLength(),
                                                   volume.getBlockLength()};
                auto ret = gpu_resource.uploadResource(desc, entry, extent, p, volume.getBlockSize(), false);
                assert(ret);
                ret = block_volume_manager.unlock(p);
                assert(ret);
                page_table.update(block_index);
            };
            // 4.1 get volume block and upload to GPUResource
            parallel_foreach(missed_blocks, task, missed_blocks.size());

            gpu_resource.flush(tid);

            volume_renderer->updatePageTable(cur_renderer_page_table);

            STOP_TIMER("volume render prepare")

            // 5 render and merge result
            // 5.1 render
            {
                START_TIMER
                volume_renderer->render(renderer_camera);
                // 5.2 release read lock for page table

                for (const auto &item : cur_renderer_page_table)
                {
                    page_table.release(item.second);
                }
                STOP_TIMER("release page table")
            }

            const auto &ret = volume_renderer->getFrameBuffers().getColors();

            return ret;
        };
    }
    else{
        volume_render = [&]()->const Image&{
            START_TIMER
            VolumeRendererCamera renderer_camera{camera};
            renderer_camera.raycasting_step = volume.getVoxel() * 0.5f;
            renderer_camera.raycasting_max_dist = camera.far_z;
            LOG_INFO("render camera position: {} {} {}", camera.position.x, camera.position.y, camera.position.z);

            // 1. get current camera view and projection matrix
            auto view_matrix = GeometryHelper::ExtractViewMatrixFromCamera(renderer_camera);
            auto proj_matrix = GeometryHelper::ExtractProjMatrixFromCamera(renderer_camera);
            auto vp = proj_matrix * view_matrix;
            FrustumExt view_frustum{};
            GeometryHelper::ExtractViewFrustumPlanesFromMatrix(vp, view_frustum);
            RenderHelper::GetDefaultLodDist(volume, renderer_camera.lod_dist.lod_dist, volume.getMaxLod());

            // 2. compute intersect blocks with current camera view frustum
            //需要得到不同lod的相交块
            auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum, renderer_camera.lod_dist,
                                                                            renderer_camera.position);
            int intersect_block_count = intersect_blocks.size();
            // todo check intersect_block_count not large than gpu texture memory limit
            LOG_INFO("volume render intersect block count: {}", intersect_block_count);
            for (auto &b : intersect_blocks)
            {
                LOG_INFO("intersect block {} {} {} {}", b.x, b.y, b.z, b.w);
            }

            // 3.2 compute cached blocks and missed blocks
            std::vector<Volume::BlockIndex> missed_blocks;
            std::vector<Renderer::PageTableItem> cur_renderer_page_table;
            auto &page_table = gpu_resource.getPageTable();
            page_table.acquireLock();
            auto query_ret = page_table.queriesAndLockExt(intersect_blocks);
            for (const auto &ret : query_ret)
            {
                if (ret.cached)
                {
                    cur_renderer_page_table.emplace_back(ret.entry, ret.value);
                }
                else
                {
                    missed_blocks.emplace_back(ret.value);
                }
            }
            LOG_INFO("first time missed blocks count {}", missed_blocks.size());

            //此时获取保证其之后不会被上传写入 一定需要此处上传
            auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks); //不一定等同于missed_blocks
            page_table.acquireRelease();
            LOG_INFO("PageTable acquire release");

            std::unordered_map<Volume::BlockIndex, PageTable::EntryItem> block_entries; //真正需要上传的数据块
            std::mutex block_entries_mtx;
            missed_blocks.clear(); //重新生成
            for (const auto &entry : missed_block_entries)
            {
                if (!entry.cached)
                {
                    block_entries[entry.value] = entry.entry;
                    missed_blocks.emplace_back(entry.value);
                }
                cur_renderer_page_table.emplace_back(entry.entry, entry.value);
            }
            LOG_INFO("volume render missed block count: {}", missed_blocks.size());

            // 4 get block and upload

            auto thread_id = std::this_thread::get_id();
            auto tid = std::hash<decltype(thread_id)>()(thread_id);
            auto task = [&](int thread_idx, Volume::BlockIndex block_index) {
                auto p = block_volume_manager.getVolumeBlockAndLock(block_index);
                assert(p);

                auto entry = block_entries[block_index];
                GPUResource::ResourceDesc desc;
                desc.id = tid;
                desc.type = GPUResource::Texture;
                desc.width = volume.getBlockLength();
                desc.pitch = volume.getBlockLength();
                desc.height = volume.getBlockLength();
                desc.depth = volume.getBlockLength();
                desc.size = volume.getBlockSize();
                GPUResource::ResourceExtent extent{volume.getBlockLength(), volume.getBlockLength(),
                                                   volume.getBlockLength()};
                auto ret = gpu_resource.uploadResource(desc, entry, extent, p, volume.getBlockSize(), false);
                assert(ret);
                ret = block_volume_manager.unlock(p);
                assert(ret);
                page_table.update(block_index);
            };
            // 4.1 get volume block and upload to GPUResource
            parallel_foreach(missed_blocks, task, missed_blocks.size());

            gpu_resource.flush(tid);

            volume_renderer->updatePageTable(cur_renderer_page_table);

            STOP_TIMER("volume render prepare")

            // 5 render and merge result
            // 5.1 render
            {
                START_TIMER
                volume_renderer->render(renderer_camera);
                // 5.2 release read lock for page table

                for (const auto &item : cur_renderer_page_table)
                {
                    page_table.release(item.second);
                }
                STOP_TIMER("release page table")
            }

            const auto &ret = volume_renderer->getFrameBuffers().getColors();

            return ret;
        };
    }

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
              switch (event.key.keysym.sym){
              case SDLK_ESCAPE:{
                  exit = true;
                  LOG_ERROR("exit camera position: {} {} {}",camera.position.x,camera.position.y,camera.position.z);
                  break;
              }
              case SDLK_w:{
                  camera.position += camera.front * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_s:{
                  camera.position -= camera.front * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_a:{
                  camera.position -= camera.right * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_d:{
                  camera.position += camera.right * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_q:{
                  camera.position += camera.up * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_e:{
                  camera.position -= camera.up * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              }
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
    while(!exit){
        last_t = SDL_GetTicks();

        process_input(exit,delta_t);
        START_TIMER
        const auto& colors = volume_render();
        STOP_TIMER("volume render")

        SDLDraw(colors);

        delta_t = SDL_GetTicks() - last_t;
        std::cout<<"delta_t: "<<delta_t<<std::endl;
    }
    LOG_INFO("exit main render loop");
}

//为离线渲染定制 异步没有意义
void RunRenderPassLoop(){
    PluginLoader::LoadPlugins("C:/Users/wyz/projects/MouseBrainVisualizeProject/bin");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path="E:/MouseNeuronData/mouse_file_config0.json";
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
    for(int i = 0;i < 10; i++)
        gpu_resource.createGPUResource(desc);
    const int gpu_resource_block_limit = 10 * 8;

    auto volume_renderer = RendererCaster<Renderer::VOLUME_EXT>::GetPtr(gpu_resource.getRenderer(Renderer::VOLUME_EXT));

    volume_renderer->setVolume(volume);

    const int frame_w = 960;
    const int frame_h = 480;
    if(!InitWindowContext(frame_w,frame_h)){
        LOG_ERROR("init window context failed");
        throw std::runtime_error("Init window context failed");
    }

    CameraExt camera{};
    camera.position = {5.3f,5.53f,8.6f};
    camera.target = {5.3f,5.53f,6.1f};
    camera.front = {0.f,0.f,-1.f};
    camera.up = {0.f,1.f,0.f};
    camera.right = {1.f,0.f,0.f};
    camera.near_z = 0.001f;
    camera.far_z = 6.f;
    camera.width = frame_w;
    camera.height = frame_h;
    camera.aspect = static_cast<float>(frame_w) / static_cast<float>(frame_h);
    camera.yaw = -90.f;
    camera.pitch = 0.f;
    camera.fov = 40.f;

    TransferFunctionExt1D transferFunctionExt1D{};
    transferFunctionExt1D.points.emplace_back(0.25f,Vector4f{0.f,1.f,0.5f,0.f});
    transferFunctionExt1D.points.emplace_back(0.6f,Vector4f{1.f,0.5f,0.f,1.f});
    ComputeTransferFunction1DExt(transferFunctionExt1D);
    volume_renderer->setTransferFunction(transferFunctionExt1D);


    auto volume_render = [&]()->const Image&{
        VolumeRendererCamera renderer_camera{camera};
        renderer_camera.raycasting_step = volume.getVoxel() * 0.5f;
        renderer_camera.raycasting_max_dist = camera.far_z;

        auto view_matrix = GeometryHelper::ExtractViewMatrixFromCamera(renderer_camera);
        auto proj_matrix = GeometryHelper::ExtractProjMatrixFromCamera(renderer_camera);
        auto vp = proj_matrix * view_matrix;
        FrustumExt view_frustum{};
        GeometryHelper::ExtractViewFrustumPlanesFromMatrix(vp, view_frustum);
        RenderHelper::GetDefaultLodDist(volume, renderer_camera.lod_dist.lod_dist, volume.getMaxLod());

        auto intersect_blocks =
            volume_block_tree.computeIntersectBlock(view_frustum, renderer_camera.lod_dist, renderer_camera.position);
        int intersect_block_count = intersect_blocks.size();
        LOG_INFO("volume render intersect block count: {}", intersect_block_count);
        for (auto &b : intersect_blocks)
        {
            LOG_INFO("intersect block {} {} {} {}", b.x, b.y, b.z, b.w);
        }
        //unordered_map begin ?
        std::unordered_map<int,std::unordered_set<Volume::BlockIndex>> lod_intersect_blocks;
        std::set<int> lods;
        std::queue<int> lods_q;
        for(auto block:intersect_blocks){
            assert(block.w>=0);
            lod_intersect_blocks[block.w].insert(block);
            lods.insert(block.w);
        }
        for(auto lod:lods){
            lods_q.push(lod);
        }
        std::set<Volume::BlockIndex> next_lod_working_blocks;
        bool newFrame = true;
        int min_lod = lods_q.front();
        while(!lods_q.empty()){
            int cur_lod = lods_q.front();
            assert(cur_lod >= 0);
            lods_q.pop();
            int next_lod = -1;
            if(!lods_q.empty()){
                next_lod = lods_q.front();
            }

            auto& cur_lod_intersect_blocks = lod_intersect_blocks[cur_lod];
            std::queue<Volume::BlockIndex> cur_working_blocks;
            auto computeStartBlock = [&](int cur_lod){
                auto &pos = renderer_camera.position;
                float min_dist = std::numeric_limits<float>::max();
                Volume::BlockIndex start_block;
                for (auto &b : lod_intersect_blocks[cur_lod])
                {
                    auto dist = VolumeHelper::ComputeDistanceToBlockCenter(volume, b, pos);
                    if (min_dist > dist)
                    {
                        min_dist = dist;
                        start_block = b;
                    }
                }
                assert(start_block.isValid());
                cur_working_blocks.push(start_block);
            };
            if(cur_lod == min_lod){
                if(VolumeHelper::VolumeSpacePositionInsideVolume(volume,renderer_camera.position)){
                    auto start_block = VolumeHelper::GetBlockIndexByVolumeSpacePosition(volume,renderer_camera.position,cur_lod);
                    assert(start_block.isValid());
                    cur_working_blocks.push(start_block);
                }
                else{
                    //find nearest block
                    computeStartBlock(cur_lod);
                }
            }
            else{
                if(next_lod_working_blocks.empty()){
                    computeStartBlock(cur_lod);
                }
                else{
                    for(auto& b:next_lod_working_blocks){
                        cur_working_blocks.push(b);
                    }
                    next_lod_working_blocks.clear();
                }
            }
            while(!cur_lod_intersect_blocks.empty()){
                if(cur_working_blocks.empty()){
                    computeStartBlock(cur_lod);
                    LOG_INFO("cur_lod: {}, intersect_blocks count: {}",cur_lod,cur_lod_intersect_blocks.size());
//                    throw std::runtime_error("cur_working_blocks get empty");
                }
                // clear cur working blocks in lod intersect blocks
                std::queue<Volume::BlockIndex> tmp;
                LOG_INFO("lod {} has block count {}",cur_lod,cur_working_blocks.size());
                while (!cur_working_blocks.empty())
                {
                    LOG_INFO("lod {} block : {} {} {}",cur_lod,cur_working_blocks.front().x,
                             cur_working_blocks.front().y,cur_working_blocks.front().z);
                    cur_lod_intersect_blocks.erase(cur_working_blocks.front());
                    tmp.push(cur_working_blocks.front());
                    cur_working_blocks.pop();
                }
                cur_working_blocks = std::move(tmp);

                // evaluate cur working blocks whether large than gpu limit
                // perform render task
                auto &page_table = gpu_resource.getPageTable();
                auto dummy_cur_working_blocks = cur_working_blocks;
                do
                {
                    int batch_count = page_table.getAvailableCount();
                    if (batch_count == 0)
                    {
                        throw std::runtime_error("page table not release correct");
                    }
                    std::vector<Volume::BlockIndex> cur_batch_working_blocks;
                    while (batch_count-- > 0 && !cur_working_blocks.empty())
                    {
                        cur_batch_working_blocks.emplace_back(cur_working_blocks.front());
                        cur_working_blocks.pop();
                    }
                    std::vector<Volume::BlockIndex> missed_blocks;
                    std::vector<Renderer::PageTableItem> cur_renderer_page_table;
                    page_table.acquireLock();
                    //must check if same blocks are queried
                    auto query_ret = page_table.queriesAndLockExt(cur_batch_working_blocks);
                    for (const auto &ret : query_ret)
                    {
                        if (ret.cached)
                        {
                            cur_renderer_page_table.emplace_back(ret.entry, ret.value);
                        }
                        else
                        {
                            missed_blocks.emplace_back(ret.value);
                        }
                    }
                    auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks);
                    page_table.acquireRelease();

                    std::unordered_map<Volume::BlockIndex, PageTable::EntryItem> block_entries;
                    missed_blocks.clear();
                    for (const auto &entry : missed_block_entries)
                    {
                        if (!entry.cached)
                        {
                            block_entries[entry.value] = entry.entry;
                            missed_blocks.emplace_back(entry.value);
                        }
                        cur_renderer_page_table.emplace_back(entry.entry, entry.value);
                    }
                    auto task = [&](int thread_idx, Volume::BlockIndex block_index) {
                        auto p = block_volume_manager.getVolumeBlockAndLock(block_index);
                        assert(p);
                        auto entry = block_entries[block_index];
                        GPUResource::ResourceDesc desc{};
                        desc.type = GPUResource::Texture;
                        desc.width = volume.getBlockLength();
                        desc.pitch = volume.getBlockLength();
                        desc.height = volume.getBlockLength();
                        desc.depth = volume.getBlockLength();
                        desc.size = volume.getBlockSize();
                        GPUResource::ResourceExtent extent{volume.getBlockLength(), volume.getBlockLength(),
                                                           volume.getBlockLength()};
                        auto ret = gpu_resource.uploadResource(desc, entry, extent, p, volume.getBlockSize(), true);
                        ret = block_volume_manager.unlock(p);
                        assert(ret);
                        page_table.update(block_index);
                    };
                    parallel_foreach(missed_blocks, task, missed_blocks.size());

                    volume_renderer->updatePageTable(cur_renderer_page_table);

                    auto ret = volume_renderer->renderPass(renderer_camera, newFrame);
                    if (newFrame)
                    {
                        newFrame = false;
                    }
                    if (ret)
                    {
                        if (next_lod == -1 && cur_lod_intersect_blocks.empty())
                        {
                            LOG_INFO("render successfully finished!");
                        }
                        else
                        {
                            //1.ray terminate early because of alpha > 0.99
                            //2.view frustum space is bigger than ray cast space
                            LOG_ERROR("renderPass return true but render is not finished!");
                        }
                    }
                    for(const auto& item:cur_renderer_page_table){
                        page_table.release(item.second);
                    }
                }while (cur_working_blocks.size() > page_table.getAvailableCount());

                // compute next renderPass blocks
                cur_working_blocks = std::move(dummy_cur_working_blocks);
                std::set<Volume::BlockIndex> next_working_blocks;
                while (!cur_working_blocks.empty())
                {
                    auto block = cur_working_blocks.front();
                    cur_working_blocks.pop();
                    // get neighbor blocks
                    std::vector<Volume::BlockIndex> neighbor_blocks;
                    VolumeHelper::GetVolumeNeighborBlocks(volume, block, neighbor_blocks);
                    // add self to avoid miss next lod blocks
                    neighbor_blocks.emplace_back(block);
                    // find if in lod_intersect_blocks
                    for (auto &b : neighbor_blocks)
                    {
                        //check current lod
                        if (cur_lod_intersect_blocks.count(b) == 1)
                        {
                            next_working_blocks.insert(b);
//                            cur_lod_intersect_blocks.erase(b);
                        }
                        //check next lod
                        if (next_lod == -1)
                            continue;
                        // check if next lod of b exists
                        // this is may be wrong because lod change not consecutive
                        Volume::BlockIndex next_lod_b = VolumeHelper::GetLodBlockIndex(b,next_lod);

                        if (lod_intersect_blocks[next_lod].count(next_lod_b) == 1)
                        {
                            next_lod_working_blocks.insert(next_lod_b);
                        }

                    }//end of checking neighbor blocks
                }
                for(auto& b:next_working_blocks){
                    cur_working_blocks.push(b);
                }
                //end of renderPass a level of BFS for the lod
            }
            LOG_INFO("lod {} has finish renderPass",cur_lod);
        }
        LOG_INFO("finish render a frame");
        return volume_renderer->getFrameBuffers().getColors();
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
              switch (event.key.keysym.sym){
              case SDLK_ESCAPE:{
                  exit = true;
                  LOG_ERROR("exit camera position: {} {} {}",camera.position.x,camera.position.y,camera.position.z);
                  break;
              }
              case SDLK_w:{
                  camera.position += camera.front * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_s:{
                  camera.position -= camera.front * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_a:{
                  camera.position -= camera.right * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_d:{
                  camera.position += camera.right * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_q:{
                  camera.position += camera.up * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              case SDLK_e:{
                  camera.position -= camera.up * 0.01f;
                  camera.target = camera.position + camera.front;
                  break;
              }
              }
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
    while(!exit){
        last_t = SDL_GetTicks();

        process_input(exit,delta_t);
        START_TIMER
        const auto& colors = volume_render();
        STOP_TIMER("volume render")

        SDLDraw(colors);

        delta_t = SDL_GetTicks() - last_t;
        std::cout<<"delta_t: "<<delta_t<<std::endl;
    }
    LOG_INFO("exit main render loop");
}
int main(int argc,char** argv){
    SET_LOG_LEVEL_INFO
    std::stringstream ss;
    ss << "usage:"
          "\n\t0 RunAsyncRenderLoop"
          "\n\t1 RunSyncRenderLoop"
          "\n\t2 RunRenderPassLoop"
       << std::endl;
    int t = -1;
    if(argc == 2){
        t = argv[1][0] - '0';
    }
    else
    {
        std::cerr << ss.str();
        exit(0);
    }
    try{
        if (t == 0)
        {
            LOG_INFO("RunAsyncRenderLoop");
            RunRenderLoop(true);
        }
        else if (t == 1)
        {
            LOG_INFO("RunSyncRenderLoop");
            RunRenderLoop(false);
        }
        else if (t == 2)
        {
            LOG_INFO("RunRenderPassLoop");
            RunRenderPassLoop();
        }
        else
        {
            std::cerr << ss.str();
            throw std::runtime_error("Unknown render type");
        }
        LOG_INFO("Exit render loop");
    }
    catch (const std::exception& err)
    {
        LOG_ERROR("{}, exit program!",err.what());
    }
    return 0;
}
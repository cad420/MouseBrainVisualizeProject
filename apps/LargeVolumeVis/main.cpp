//
// Created by wyz on 2022/2/25.
//
#include "core/BlockVolumeManager.hpp"
#include "core/Renderer.hpp"
#include "core/VolumeBlockTree.hpp"
#include "plugin/PluginLoader.hpp"
#include "core/GPUNode.hpp"
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
    wc->frame = SDL_CreateTexture(wc->renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,w,h);
    if(!wc->frame){
        LOG_ERROR("create sdl texture failed");
        return false;
    }
    return true;
}


int main(int argc,char** argv){

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

    block_volume_manager.init(std::move(p));

    GPUResource gpu_resource(0);

    GPUNode gpu_node(0);

    auto volume_renderer = VolumeRenderer::create(gpu_resource);

    const int frame_w = 1280;
    const int frame_h = 720;
    if(!InitWindowContext(frame_w,frame_h)){
        LOG_ERROR("init window context failed and exit");
        return -1;
    }

    Camera camera{};

    auto volume_render = [&](){

        //get current camera view and projection matrix
        auto vp = Matrix4f();
        Frustum view_frustum{};
        GeometryHelper::ExtractViewFrustumPlanesFromMatrix(vp,view_frustum);


        auto missed_blocks = volume_block_tree.computeIntersectBlock(view_frustum);
        int missed_block_count = missed_blocks.size();
        LOG_INFO("volume render missed block count: {}",missed_block_count);

        //todo 在这里可以根据GPUNode的数量分割渲染任务 交给不同的GPU
        //分割任务算法可以根据不同渲染器的类别应用
        //暂时不考虑多个GPU数量
        //use GPUNode and PageTable here

        /**
         * 需要多线程获取VolumeBlock数据 但是需要等所有任务都结束后再继续渲染
         */
        std::unordered_map<Volume::BlockIndex,void*> blocks;
        std::mutex mtx;
        auto decode_task = [&](int thread_idx,Volume::BlockIndex block_index){
            /**
             * 这里获取数据的任务调度由BlockVolumeManager具体负责
             */
            auto ptr = block_volume_manager.getVolumeBlockAndLock(block_index);
            std::lock_guard<std::mutex> lk(mtx);
            blocks[block_index] = ptr;
        };
        parallel_foreach(missed_blocks,decode_task,missed_block_count);

        //todo upload blocks into GPUResource, may use multi-thread to accelerate
        //在这里只需要将数据块上传到相应的GPUResource即可

        auto& page_table = gpu_node.getPageTable();

        //upload resource sync
        for(auto& block:blocks){
            page_table.lock(block.first);
            PageTable::EntryItem table_entry;
            bool cached = page_table.getEntryItem(block.first,table_entry);
            if(cached) continue;
            gpu_resource.uploadResource(GPUResource::Texture,table_entry,block.second,volume.getBlockSize(),true);
            page_table.release(table_entry);
            page_table.update(table_entry,block.first);
        }
        //upload resource async
//        std::vector<std::pair<PageTable::EntryItem,PageTable::ValueItem>> book;
//        for(auto& block:blocks){
//          page_table.lock(block.first);
//          PageTable::EntryItem table_entry;
//          bool cached = page_table.getEntryItem(block.first,table_entry);
//          book.emplace_back({table_entry,block.first});
//          if(cached) continue;
//          gpu_resource.uploadResource(GPUResource::Texture,table_entry,block.second,volume.getBlockSize(),false);
//        }
//        gpu_resource.flush();
//        page_table.releaseAll();
//        for(auto& b:book) page_table.update(b.first,b.second);

        volume_renderer->render(camera);

        //unlock the blocks
        {
            int unlock_failed_count = 0;
            for (const auto &block : blocks)
            {
                if (!block_volume_manager.unlock(block.second))
                {
                    unlock_failed_count++;
                    LOG_ERROR("unlock block failed: {} {} {} {}", block.first.x, block.first.y, block.first.z,
                              block.first.w);
                }
            }
            if (unlock_failed_count > 0)
            {
                LOG_ERROR("{} blocks failed to unlock", unlock_failed_count);
            }
        }

        auto& ret = volume_renderer->getFrameBuffers().getColors();

        /**
         * 在这里如果是切分任务渲染的话 需要对渲染结果进行融合评级
         */

        return ret;
    };
    bool exit = false;
    uint32_t delta_t = 0;
    uint32_t last_t = 0;
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

        const auto& colors =  volume_render();

        sdl_draw(colors);

        delta_t = SDL_GetTicks() - last_t;
    }

    return 0;
}
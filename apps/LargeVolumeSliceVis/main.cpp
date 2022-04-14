//
// Created by wyz on 2022/3/11.
//

#include "algorithm/ColorMapping.hpp"
#include "algorithm/GeometryHelper.hpp"
#include "algorithm/RenderHelper.hpp"
#include "algorithm/SliceHelper.hpp"
#include "common/Logger.hpp"
#include "common/Parrallel.hpp"
#include "core/BlockVolumeManager.hpp"
#include "core/GPUResource.hpp"
#include "core/VolumeBlockTree.hpp"
#include "plugin/PluginLoader.hpp"
#include "utils/Timer.hpp"
#include <SDL.h>
#include <future>
using namespace mrayns;

struct WindowContext
{
    SDL_Window *window{nullptr};
    SDL_Renderer *renderer{nullptr};
    SDL_Texture *frame{nullptr};
    int width, height;
};
WindowContext window_context;
WindowContext *wc = &window_context;

bool InitWindowContext(int w, int h)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        LOG_ERROR("sdl init failed");
        return false;
    }
    wc->width = w;
    wc->height = h;
    wc->window = SDL_CreateWindow("LargeVolumeSliceVis", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, 0);
    if (!wc->window)
    {
        LOG_ERROR("create sdl window failed");
        return false;
    }
    wc->renderer = SDL_CreateRenderer(wc->window, -1, SDL_RENDERER_ACCELERATED);
    if (!wc->renderer)
    {
        LOG_ERROR("create sdl renderer failed");
        return false;
    }
    // sdl ABGR8888 packed from high to low bit so it equal to RGBA32
    // vulkan RGBA888 is from low to high so it equal to sdl RGBA32
    wc->frame = SDL_CreateTexture(wc->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!wc->frame)
    {
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
void RunRenderLoop(bool async)
{
    PluginLoader::LoadPlugins("C:/Users/wyz/projects/MouseBrainVisualizeProject/bin");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path = "E:/MouseNeuronData/mouse_file_config0.json";
    p->open(h264_file_path);
    assert(p.get());

    auto &host_node = HostNode::getInstance();
    host_node.setGPUNum(1);

    p->setHostNode(&host_node);

    auto volume = p->getVolume();

    VolumeBlockTree volume_block_tree;
    volume_block_tree.buildTree(volume);

    auto &block_volume_manager = BlockVolumeManager::getInstance();

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
    for (int i = 0; i < 16; i++)
        gpu_resource.createGPUResource(desc);

    auto slice_renderer = RendererCaster<Renderer::SLICE>::GetPtr(gpu_resource.getRenderer(Renderer::SLICE));
    slice_renderer->setVolume(volume);

    const int window_w = SliceRenderer::MaxSliceW;
    const int window_h = SliceRenderer::MaxSliceH;
    if (!InitWindowContext(window_w, window_h))
    {
        LOG_ERROR("init window context failed");
        throw std::runtime_error("Init window context failed");
    }

    Slice slice{};
    slice.n_pixels_w = window_w;
    slice.n_pixels_h = window_h;
    slice.voxels_per_pixel = 2.f;
    slice.region = {0, 0, window_w - 1, window_h - 1};
    slice.x_dir = {1.f, 0.f, 0.f};
    slice.y_dir = {0.f, 1.f, 0.f};
    slice.normal = {0.f, 0.f, 1.f};
    slice.origin = {5.52f, 5.52f, 5.9f};

    TransferFunction transferFunction{};
    transferFunction.points.emplace_back(0.25f, Vector4f{1.f, 0.f, 0.f, 0.f});
    transferFunction.points.emplace_back(0.6f, Vector4f{0.f, 1.f, 0.f, 1.f});
    slice_renderer->setTransferFunction(transferFunction);

    uint32_t render_type = 0;

    std::function<const Image &()> slice_render;

    if (async)
    {
        slice_render = [&]() -> const Image & { //如果不显示指定返回类型 lambda的函数返回类型会是Image 因为function =
                                                //不要求返回类型一定相同 它会去引用
            SliceExt sliceExt{slice, SliceHelper::GetSliceLod(slice),
                              volume.getVoxel() * SliceHelper::SliceStepVoxelRatio, 0.f};
            FrustumExt view_frustum{};
            SliceHelper::ExtractViewFrustumExtFromSliceExt(sliceExt, view_frustum, volume.getVoxel());

            auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum, sliceExt.lod);
            LOG_INFO("slice render intersect block count: {}", intersect_blocks.size());
            for (auto &b : intersect_blocks)
            {
                //              LOG_INFO("intersect block {} {} {} {}",b.x,b.y,b.z,b.w);
            }

            //获取页表位置 整个过程需要先acquireLock再acquireRelease 该期间对页表的操作是单线程的
            auto &page_table = gpu_resource.getPageTable();
            page_table.acquireLock();

            std::vector<Volume::BlockIndex> missed_blocks;
            std::vector<Renderer::PageTableItem> cur_renderer_page_table;
            //查询页表中已经有的数据块 生成目前页表不存在的缺失块
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
            LOG_INFO("first time missed blocks count: {}", missed_blocks.size());

            //异步加载数据块 先查询缺失块是否已经加载到内存中
            std::unordered_map<Volume::BlockIndex, void *> missed_block_buffer;
            std::vector<Volume::BlockIndex> copy_missed_blocks;
            copy_missed_blocks.swap(missed_blocks);
            for (const auto &block : copy_missed_blocks)
            {
                auto p = block_volume_manager.getVolumeBlock(block, false);
                if (p && block_volume_manager.lock(p))
                {
                    missed_blocks.emplace_back(block);
                }
                else
                {
                    p = nullptr;
                }
                missed_block_buffer[block] = p;
            }

            auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks);
            page_table.acquireRelease();
            assert(missed_block_entries.size() == missed_blocks.size());

            std::unordered_map<Volume::BlockIndex, PageTable::EntryItem> block_entries;
            missed_blocks.clear();
            for (const auto &entry : missed_block_entries)
            {
                if (!entry.cached)
                {
                    missed_blocks.emplace_back(entry.value);
                    block_entries[entry.value] = entry.entry;
                }
                else
                {
                    block_volume_manager.unlock(missed_block_buffer[entry.value]);
                    missed_block_buffer[entry.value] = nullptr;
                }
                cur_renderer_page_table.emplace_back(entry.entry, entry.value);
            }
            LOG_INFO("slice render missed block count: {}", missed_blocks.size());

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

            parallel_foreach(missed_blocks, task, missed_blocks.size());

            gpu_resource.flush(tid);

            slice_renderer->updatePageTable(cur_renderer_page_table);

            slice_renderer->render(sliceExt, static_cast<SliceRenderer::RenderType>(render_type));

            for (const auto &item : cur_renderer_page_table)
            {
                page_table.release(item.second);
            }

            const auto &ret = slice_renderer->getFrameBuffers().getColors();

            return ret;
        };
    }
    else
    {
        //同步框架 确保每一帧绘制完整
        slice_render = [&]() -> const Image & {
            SliceExt sliceExt{slice, SliceHelper::GetSliceLod(slice),
                              volume.getVoxel() * SliceHelper::SliceStepVoxelRatio, 0.f};
            FrustumExt view_frustum{};
            SliceHelper::ExtractViewFrustumExtFromSliceExt(sliceExt, view_frustum, volume.getVoxel());

            auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum, sliceExt.lod);
            //            LOG_INFO("slice render intersect block count: {}",intersect_blocks.size());
            //            for(auto& b:intersect_blocks){
            //              LOG_INFO("intersect block {} {} {} {}",b.x,b.y,b.z,b.w);
            //            }

            //将slice分割成与GPU个数相等的子切片 使得最后真正需要上传的数据块数量最小
            //需要快速找到最优解 如果最优解太耗时 则快速求近似解

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
            //            LOG_INFO("first time missed blocks count: {}",missed_blocks.size());

            //同步加载 该帧内会等待数据块加载好 因此对所有缺失的数据块进行页表项申请

            auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks);
            page_table.acquireRelease(); // acquire lock and release 之间不能有耗时的操作

            //在queriesAndLockExt和getEntriesAndLock之间可能有正在被上传的数据块刚好上传完
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

            auto thread_id = std::this_thread::get_id();
            auto tid = std::hash<decltype(thread_id)>()(thread_id);

            auto task = [&](int thread_idx, Volume::BlockIndex block_index) {
                LOG_INFO("start {} {} {} {}", block_index.x, block_index.y, block_index.z, block_index.w);
                auto p = block_volume_manager.getVolumeBlockAndLock(block_index);

                auto entry = block_entries[block_index];

                GPUResource::ResourceDesc desc{};
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
                LOG_INFO("finish {} {} {} {}", block_index.x, block_index.y, block_index.z, block_index.w);
            };
            parallel_foreach(missed_blocks, task, missed_blocks.size());
            //            LOG_INFO("finish parallel task");
            gpu_resource.flush(tid);
            //            LOG_INFO("finish flush");
            slice_renderer->updatePageTable(cur_renderer_page_table);

            slice_renderer->render(sliceExt, static_cast<SliceRenderer::RenderType>(render_type));

            for (const auto &item : cur_renderer_page_table)
            {
                page_table.release(item.second);
            }

            const auto &ret = slice_renderer->getFrameBuffers().getColors();

            return ret;
        };
    }

    bool exit = false;
    uint32_t delta_t = 0;
    uint32_t last_t = 0;
    float slice_move_speed = 0.001f;
    static float voxel = volume.getVoxel();
    auto process_input = [&](bool &exit, uint32_t delta_t) {
        static SDL_Event event;
        static bool zoom = false;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT: {
                exit = true;
                break;
            }
            case SDL_DROPFILE: {

                break;
            }
            case SDL_KEYDOWN: {
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE: {
                    exit = true;

                    break;
                }
                case SDLK_w: {

                    break;
                }
                case SDLK_s: {

                    break;
                }
                case SDLK_c: {
                    render_type = 1 - render_type;
                    break;
                }
                case SDLK_LCTRL: {
                    zoom = true;
                    break;
                }
                }
                break;
            }
            case SDL_KEYUP: {
                switch (event.key.keysym.sym)
                {
                case SDLK_LCTRL: {
                    zoom = false;
                    break;
                }
                }
                break;
            }
            case SDL_MOUSEWHEEL: {
                if (zoom)
                {
                    float offset = event.wheel.y;
                    float r;
                    if (offset > 0.f)
                        r = 1.25f;
                    else
                        r = 0.75;
                    slice.voxels_per_pixel *= r;
                    slice.voxels_per_pixel = (std::min)(slice.voxels_per_pixel, 64.f);
                    slice.voxels_per_pixel = (std::max)(slice.voxels_per_pixel, 1.f);
                    LOG_INFO("voxels_per_pixel: {}", slice.voxels_per_pixel);
                }
                else
                {
                    float offset = event.wheel.y;
                    slice.origin += slice.normal * offset * slice.voxels_per_pixel * voxel;
                }

                break;
            }
            case SDL_MOUSEMOTION: {
                if (event.motion.state & SDL_BUTTON_RMASK)
                {
                    // in pixel
                    float x_offset = static_cast<float>(event.motion.xrel);
                    float y_offset = static_cast<float>(event.motion.yrel);

                    slice.origin += -slice.x_dir * x_offset * slice.voxels_per_pixel * voxel -
                                    slice.y_dir * y_offset * slice.voxels_per_pixel * voxel;
                }

                break;
            }
            }
        }
    };

    while (!exit)
    {
        last_t = SDL_GetTicks();

        process_input(exit, delta_t);
        START_TIMER
        const auto &colors = slice_render();
        //        STOP_TIMER("slice render")

        SDLDraw(colors);

        delta_t = SDL_GetTicks() - last_t;
        //        std::cout<<"delta_t: "<<delta_t<<std::endl;
    }
}
//单个GPUResource下多个renderer被同时使用
void RunMultiThreadingRenderLoop(bool async)
{
    PluginLoader::LoadPlugins("C:/Users/wyz/projects/MouseBrainVisualizeProject/bin");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path = "E:/MouseNeuronData/mouse_file_config0.json";
    p->open(h264_file_path);
    assert(p.get());

    auto &host_node = HostNode::getInstance();
    host_node.setGPUNum(1);

    p->setHostNode(&host_node);

    auto volume = p->getVolume();

    VolumeBlockTree volume_block_tree;
    volume_block_tree.buildTree(volume);

    auto &block_volume_manager = BlockVolumeManager::getInstance();

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
    for (int i = 0; i < 12; i++)
        gpu_resource.createGPUResource(desc);

    std::vector<SliceRenderer *> slice_renderers;
    slice_renderers.emplace_back(RendererCaster<Renderer::SLICE>::GetPtr(gpu_resource.getRenderer(Renderer::SLICE)));
    slice_renderers.emplace_back(RendererCaster<Renderer::SLICE>::GetPtr(gpu_resource.getRenderer(Renderer::SLICE)));
    LOG_INFO("SliceRenderer 0: {}, 1: {}", (size_t)slice_renderers[0], (size_t)slice_renderers[1]);

    TransferFunction transferFunction{};
    transferFunction.points.emplace_back(0.25f, Vector4f{1.f, 0.f, 0.f, 0.f});
    transferFunction.points.emplace_back(0.6f, Vector4f{0.f, 1.f, 0.f, 1.f});

    for (auto slice_renderer : slice_renderers)
    {
        slice_renderer->setVolume(volume);
        slice_renderer->setTransferFunction(transferFunction);
    }

    const int window_w = SliceRenderer::MaxSliceW;
    const int window_h = SliceRenderer::MaxSliceH;
    if (!InitWindowContext(window_w, window_h))
    {
        LOG_ERROR("init window context failed");
        throw std::runtime_error("Init window context failed");
    }

    Slice slice{};
    slice.n_pixels_w = window_w;
    slice.n_pixels_h = window_h;
    slice.voxels_per_pixel = 2.f;
    slice.region = {0, 0, window_w - 1, window_h - 1};
    slice.x_dir = {1.f, 0.f, 0.f};
    slice.y_dir = {0.f, 1.f, 0.f};
    slice.normal = {0.f, 0.f, 1.f};
    slice.origin = {5.52f, 5.52f, 5.9f};

    std::function<const Image &()> slice_render;

    uint32_t render_type = 0;

    const int slice_renderer_count = slice_renderers.size();

    if (async)
    {
    }
    else
    {
        slice_render = [&]() -> const Image & {
            static Image merge_color(window_w, window_h);

            SliceExt sliceExt{slice, SliceHelper::GetSliceLod(slice),
                              volume.getVoxel() * SliceHelper::SliceStepVoxelRatio, 0.f};
            //进行Slice分割 使用最简单的均分
            std::vector<SliceExt> sub_slices;
            SliceHelper::UniformDivideSlice(sliceExt, slice_renderer_count, sub_slices);
            for (int i = 0; i < sub_slices.size(); i++)
            {
                sub_slices[i].id = 100 + i;
            }

            auto slice_render_task = [&](const SliceExt &sub_slice, SliceRenderer *slice_renderer) -> const Image & {
                FrustumExt view_frustum{};
                SliceHelper::ExtractViewFrustumExtFromSliceExt(sub_slice, view_frustum, volume.getVoxel());

                auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum, sub_slice.lod);
                LOG_INFO("slice render intersect block count: {}", intersect_blocks.size());
                for (auto &b : intersect_blocks)
                {
                    LOG_INFO("slice id: {},intersect block {} {} {} {}", sub_slice.id, b.x, b.y, b.z, b.w);
                }
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
                LOG_INFO("first time missed blocks count: {}", missed_blocks.size());

                auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks);
                page_table.acquireRelease(); // acquire lock and release 之间不能有耗时的操作

                //在queriesAndLockExt和getEntriesAndLock之间可能有正在被上传的数据块刚好上传完
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

                auto thread_id = std::this_thread::get_id();
                auto tid = std::hash<decltype(thread_id)>()(thread_id);

                auto task = [&](int thread_idx, Volume::BlockIndex block_index) {
                    auto p = block_volume_manager.getVolumeBlockAndLock(block_index);

                    auto entry = block_entries[block_index];

                    GPUResource::ResourceDesc desc{};
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
                parallel_foreach(missed_blocks, task, missed_blocks.size());

                gpu_resource.flush(tid);

                slice_renderer->updatePageTable(cur_renderer_page_table);
                LOG_DEBUG("after update page table");
                slice_renderer->render(sliceExt, static_cast<SliceRenderer::RenderType>(sub_slice.id-100));

                for (const auto &item : cur_renderer_page_table)
                {
                    page_table.release(item.second);
                }

                return slice_renderer->getFrameBuffers().getColors();
            };
            std::vector<std::future<const Image &>> sub_images;
            for (int i = 0; i < slice_renderer_count; i++)
            {
                sub_images.emplace_back(
                    std::async(std::launch::async, slice_render_task, sub_slices[i], slice_renderers[i]));
            }

            std::vector<const Image *> sub_colors;
            for (auto &r : sub_images)
            {
                sub_colors.emplace_back(&r.get());
            }

            START_TIMER
            SliceHelper::UniformMergeSlice(sub_slices, sub_colors, merge_color);
            STOP_TIMER("merge slice")

            return merge_color;
        };
    }
    bool exit = false;
    uint32_t delta_t = 0;
    uint32_t last_t = 0;
    float slice_move_speed = 0.001f;
    static float voxel = volume.getVoxel();
    auto process_input = [&](bool &exit, uint32_t delta_t) {
        static SDL_Event event;
        static bool zoom = false;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT: {
                exit = true;
                break;
            }
            case SDL_DROPFILE: {

                break;
            }
            case SDL_KEYDOWN: {
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE: {
                    exit = true;

                    break;
                }
                case SDLK_w: {

                    break;
                }
                case SDLK_s: {

                    break;
                }
                case SDLK_c: {
                    render_type = 1 - render_type;
                    break;
                }
                case SDLK_LCTRL: {
                    zoom = true;
                    break;
                }
                }
                break;
            }
            case SDL_KEYUP: {
                switch (event.key.keysym.sym)
                {
                case SDLK_LCTRL: {
                    zoom = false;
                    break;
                }
                }
                break;
            }
            case SDL_MOUSEWHEEL: {
                if (zoom)
                {
                    float offset = event.wheel.y;
                    float r;
                    if (offset > 0.f)
                        r = 1.25f;
                    else
                        r = 0.75;
                    slice.voxels_per_pixel *= r;
                    slice.voxels_per_pixel = (std::min)(slice.voxels_per_pixel, 64.f);
                    slice.voxels_per_pixel = (std::max)(slice.voxels_per_pixel, 1.f);
                    LOG_INFO("voxels_per_pixel: {}", slice.voxels_per_pixel);
                }
                else
                {
                    float offset = event.wheel.y;
                    slice.origin += slice.normal * offset * slice.voxels_per_pixel * voxel;
                }

                break;
            }
            case SDL_MOUSEMOTION: {
                if (event.motion.state & SDL_BUTTON_RMASK)
                {
                    // in pixel
                    float x_offset = static_cast<float>(event.motion.xrel);
                    float y_offset = static_cast<float>(event.motion.yrel);

                    slice.origin += -slice.x_dir * x_offset * slice.voxels_per_pixel * voxel -
                                    slice.y_dir * y_offset * slice.voxels_per_pixel * voxel;
                }

                break;
            }
            }
        }
    };

    while (!exit)
    {
        last_t = SDL_GetTicks();

        process_input(exit, delta_t);
        START_TIMER
        const auto &colors = slice_render();
        STOP_TIMER("slice render")

        SDLDraw(colors);

        delta_t = SDL_GetTicks() - last_t;
        std::cout << "delta_t: " << delta_t << std::endl;
    }
}

void RunDivideAndMergeRenderLoop(bool async)
{
    PluginLoader::LoadPlugins("C:/Users/wyz/projects/MouseBrainVisualizeProject/bin");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path = "E:/MouseNeuronData/mouse_file_config0.json";
    p->open(h264_file_path);
    assert(p.get());

    auto &host_node = HostNode::getInstance();
    host_node.setGPUNum(1);

    p->setHostNode(&host_node);

    auto volume = p->getVolume();

    VolumeBlockTree volume_block_tree;
    volume_block_tree.buildTree(volume);

    auto &block_volume_manager = BlockVolumeManager::getInstance();

    block_volume_manager.setProvider(std::move(p));

    block_volume_manager.init();

    int gpu_count = 2;
    std::vector<std::unique_ptr<GPUResource>> gpu_resources;
    for (int i = 0; i < gpu_count; i++)
    {
        gpu_resources.emplace_back(std::make_unique<GPUResource>(0));
    }
    GPUResource::ResourceDesc desc{};
    desc.type = mrayns::GPUResource::Texture;
    desc.width = 1024;
    desc.height = 1024;
    desc.depth = 1024;
    desc.pitch = 1024;
    desc.block_length = volume.getBlockLength();
    for (int i = 0; i < 5; i++)
    {
        for (auto &gpu_resc : gpu_resources)
        {
            gpu_resc->createGPUResource(desc);
        }
    }
    std::vector<SliceRenderer *> slice_renderers;
    for (auto &gpu_resc : gpu_resources)
    {
        slice_renderers.emplace_back(RendererCaster<Renderer::SLICE>::GetPtr(gpu_resc->getRenderer(Renderer::SLICE)));
    }

    TransferFunction transferFunction{};
    transferFunction.points.emplace_back(0.25f, Vector4f{1.f, 0.f, 0.f, 0.f});
    transferFunction.points.emplace_back(0.6f, Vector4f{0.f, 1.f, 0.f, 1.f});

    for (auto slice_renderer : slice_renderers)
    {
        slice_renderer->setVolume(volume);
        slice_renderer->setTransferFunction(transferFunction);
    }

    const int window_w = SliceRenderer::MaxSliceW;
    const int window_h = SliceRenderer::MaxSliceH;
    if (!InitWindowContext(window_w, window_h))
    {
        LOG_ERROR("init window context failed");
        throw std::runtime_error("Init window context failed");
    }

    Slice slice{};
    slice.n_pixels_w = window_w;
    slice.n_pixels_h = window_h;
    slice.voxels_per_pixel = 2.f;
    slice.region = {0, 0, window_w - 1, window_h - 1};
    slice.x_dir = {1.f, 0.f, 0.f};
    slice.y_dir = {0.f, 1.f, 0.f};
    slice.normal = {0.f, 0.f, 1.f};
    slice.origin = {5.52f, 5.52f, 5.9f};

    std::function<const Image &()> slice_render;

    uint32_t render_type = 0;

    if (async)
    {
    }
    else
    {
        slice_render = [&]() -> const Image & {
            static Image merge_color(window_w, window_h);

            SliceExt sliceExt{slice, SliceHelper::GetSliceLod(slice),
                              volume.getVoxel() * SliceHelper::SliceStepVoxelRatio, 0.f};
            //进行Slice分割 使用最简单的均分
            std::vector<SliceExt> sub_slices;
            SliceHelper::UniformDivideSlice(sliceExt, gpu_count, sub_slices);
            for (int i = 0; i < sub_slices.size(); i++)
            {
                sub_slices[i].id = 100 + i;
            }
            auto slice_render_task = [&](const SliceExt &sub_slice, SliceRenderer *slice_renderer,
                                         GPUResource *gpu_resource) -> const Image & {
                FrustumExt view_frustum{};
                SliceHelper::ExtractViewFrustumExtFromSliceExt(sub_slice, view_frustum, volume.getVoxel());

                auto intersect_blocks = volume_block_tree.computeIntersectBlock(view_frustum, sub_slice.lod);
                LOG_INFO("slice render intersect block count: {}", intersect_blocks.size());
                for (auto &b : intersect_blocks)
                {
                    LOG_INFO("slice id: {},intersect block {} {} {} {}", sub_slice.id, b.x, b.y, b.z, b.w);
                }
                std::vector<Volume::BlockIndex> missed_blocks;
                std::vector<Renderer::PageTableItem> cur_renderer_page_table;
                auto &page_table = gpu_resource->getPageTable();
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
                LOG_INFO("first time missed blocks count: {}", missed_blocks.size());

                auto missed_block_entries = page_table.getEntriesAndLock(missed_blocks);
                page_table.acquireRelease(); // acquire lock and release 之间不能有耗时的操作

                //在queriesAndLockExt和getEntriesAndLock之间可能有正在被上传的数据块刚好上传完
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

                auto thread_id = std::this_thread::get_id();
                auto tid = std::hash<decltype(thread_id)>()(thread_id);

                auto task = [&](int thread_idx, Volume::BlockIndex block_index) {
                    auto p = block_volume_manager.getVolumeBlockAndLock(block_index);

                    auto entry = block_entries[block_index];

                    GPUResource::ResourceDesc desc{};
                    desc.id = tid;
                    desc.type = GPUResource::Texture;
                    desc.width = volume.getBlockLength();
                    desc.pitch = volume.getBlockLength();
                    desc.height = volume.getBlockLength();
                    desc.depth = volume.getBlockLength();
                    desc.size = volume.getBlockSize();
                    GPUResource::ResourceExtent extent{volume.getBlockLength(), volume.getBlockLength(),
                                                       volume.getBlockLength()};
                    auto ret = gpu_resource->uploadResource(desc, entry, extent, p, volume.getBlockSize(), false);
                    assert(ret);
                    ret = block_volume_manager.unlock(p);
                    assert(ret);
                    page_table.update(block_index);
                };
                parallel_foreach(missed_blocks, task, missed_blocks.size());

                gpu_resource->flush(tid);

                slice_renderer->updatePageTable(cur_renderer_page_table);

                slice_renderer->render(sliceExt, static_cast<SliceRenderer::RenderType>(render_type));

                for (const auto &item : cur_renderer_page_table)
                {
                    page_table.release(item.second);
                }

                return slice_renderer->getFrameBuffers().getColors();
            };

            std::vector<std::future<const Image &>> sub_images;
            for (int i = 0; i < gpu_count; i++)
            {
                sub_images.emplace_back(std::async(std::launch::async, slice_render_task, sub_slices[i],
                                                   slice_renderers[i], gpu_resources[i].get()));
            }

            std::vector<const Image *> sub_colors;
            for (auto &r : sub_images)
            {
                sub_colors.emplace_back(&r.get());
            }

            START_TIMER
            SliceHelper::UniformMergeSlice(sub_slices, sub_colors, merge_color);
            STOP_TIMER("merge slice")

            return merge_color;
        };
    }

    bool exit = false;
    uint32_t delta_t = 0;
    uint32_t last_t = 0;
    float slice_move_speed = 0.001f;
    static float voxel = volume.getVoxel();
    auto process_input = [&](bool &exit, uint32_t delta_t) {
        static SDL_Event event;
        static bool zoom = false;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT: {
                exit = true;
                break;
            }
            case SDL_DROPFILE: {

                break;
            }
            case SDL_KEYDOWN: {
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE: {
                    exit = true;

                    break;
                }
                case SDLK_w: {

                    break;
                }
                case SDLK_s: {

                    break;
                }
                case SDLK_c: {
                    render_type = 1 - render_type;
                    break;
                }
                case SDLK_LCTRL: {
                    zoom = true;
                    break;
                }
                }
                break;
            }
            case SDL_KEYUP: {
                switch (event.key.keysym.sym)
                {
                case SDLK_LCTRL: {
                    zoom = false;
                    break;
                }
                }
                break;
            }
            case SDL_MOUSEWHEEL: {
                if (zoom)
                {
                    float offset = event.wheel.y;
                    float r;
                    if (offset > 0.f)
                        r = 1.25f;
                    else
                        r = 0.75;
                    slice.voxels_per_pixel *= r;
                    slice.voxels_per_pixel = (std::min)(slice.voxels_per_pixel, 64.f);
                    slice.voxels_per_pixel = (std::max)(slice.voxels_per_pixel, 1.f);
                    LOG_INFO("voxels_per_pixel: {}", slice.voxels_per_pixel);
                }
                else
                {
                    float offset = event.wheel.y;
                    slice.origin += slice.normal * offset * slice.voxels_per_pixel * voxel;
                }

                break;
            }
            case SDL_MOUSEMOTION: {
                if (event.motion.state & SDL_BUTTON_RMASK)
                {
                    // in pixel
                    float x_offset = static_cast<float>(event.motion.xrel);
                    float y_offset = static_cast<float>(event.motion.yrel);

                    slice.origin += -slice.x_dir * x_offset * slice.voxels_per_pixel * voxel -
                                    slice.y_dir * y_offset * slice.voxels_per_pixel * voxel;
                }

                break;
            }
            }
        }
    };

    while (!exit)
    {
        last_t = SDL_GetTicks();

        process_input(exit, delta_t);
        START_TIMER
        const auto &colors = slice_render();
        STOP_TIMER("slice render")

        SDLDraw(colors);

        delta_t = SDL_GetTicks() - last_t;
        std::cout << "delta_t: " << delta_t << std::endl;
    }
}

int main(int argc, char **argv)
{
    std::stringstream ss;
    ss << "usage:"
          "\n\t0 RunAsyncRenderLoop"
          "\n\t1 RunSyncRenderLoop"
          "\n\t2 RunASyncDivideAndMergeRenderLoop"
          "\n\t3 RunSyncDivideAndMergeRenderLoop"
          "\n\t4 RunAsyncMultiThreadingRenderLoop"
          "\n\t5 RunSyncMultiThreadingRenderLoop"
       << std::endl;

    SET_LOG_LEVEL_DEBUG
    int t = -1;
    if (argc == 2)
    {
        t = argv[1][0] - '0';
    }
    else
    {
        std::cerr << ss.str();
        exit(0);
    }
    try
    {
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
            LOG_INFO("RunAsyncDivideAndMergeRenderLoop");
            RunDivideAndMergeRenderLoop(true);
        }
        else if (t == 3)
        {
            LOG_INFO("RunSyncDivideAndMergeRenderLoop");
            RunDivideAndMergeRenderLoop(false);
        }
        else if (t == 4)
        {
            LOG_INFO("RunAsyncMultiThreadingRenderLoop");
            RunMultiThreadingRenderLoop(true);
        }
        else if (t == 5)
        {
            LOG_INFO("RunSyncMultiThreadingRenderLoop");
            RunMultiThreadingRenderLoop(false);
        }
        else
        {
            std::cerr << ss.str();
            throw std::runtime_error("Unknown render type");
        }
        LOG_INFO("Exit render loop");
    }
    catch (const std::exception &err)
    {
        LOG_ERROR("{}, exit program!", err.what());
    }
    return 0;
}
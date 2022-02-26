//
// Created by wyz on 2022/2/24.
//
#pragma once
#include "PageTable.hpp"
#include <memory>
#include <vector>
MRAYNS_BEGIN
/**
 * @brief This will create vulkan GPU resources like buffer or texture.
 * It will create a vulkan instance which should be used for Renderer.
 * 一个GPU只可以创建一个GPUResource 即一个Vulkan Instance
 * 渲染器应该在GPUResource的基础上创建 即共用一个Vulkan Instance 才可以共享Vulkan资源
 * 但是一个GPU可以创建多个Renderer 即多个Renderer共用一个GPUResource
 */
class Renderer;
class GPUResource{
  public:
    explicit GPUResource(int index);
    ~GPUResource();

    int getGPUIndex() const;

    enum ResourceType{
        Buffer,Texture
    };
    struct ResourceDesc{
        ResourceType type;
        int    num;
        size_t total_size;//in bytes
        size_t single_size;
        size_t width_bytes;
        size_t height_bytes;
        size_t depth_bytes;
    };
    bool createGPUResource(ResourceType type,size_t size);

    ResourceDesc getGPUResourceDesc(ResourceType type) const;


    /**
     * @param sync if false must call flush to finish task
     */
    void uploadResource(ResourceType type,PageTable::EntryItem entryItem,void* src,size_t size,bool sync);

    void flush();

    void downloadResource(ResourceType type,PageTable::EntryItem entryItem,void* dst,size_t size,bool sync);

    bool registerRenderer(Renderer*);

    std::vector<Renderer*> getRenderers();

    void removeRenderer(Renderer*);
};

MRAYNS_END
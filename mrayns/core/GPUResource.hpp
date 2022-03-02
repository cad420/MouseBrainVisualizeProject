//
// Created by wyz on 2022/2/24.
//
#pragma once

#include <memory>
#include <vector>
#include "GPUNode.hpp"
#include "Renderer.hpp"
MRAYNS_BEGIN
/**
 * @brief This will create vulkan GPU resources like buffer or texture.
 * It will create a vulkan instance which should be used for Renderer.
 * 一个GPU只可以创建一个GPUResource 即一个Vulkan Instance
 * 渲染器应该在GPUResource的基础上创建 即共用一个Vulkan Instance 才可以共享Vulkan资源
 * 但是一个GPU可以创建多个Renderer 即多个Renderer共用一个GPUResource
 *
 * GPUResource只是个容器 内部数据的存储管理交由PageTable负责
 * GPUResource内部有一个GPUNode
 * 任何对GPUResource实质性的内存更改都会改变GPUNode GPUNode会改变其内嵌的PageTable
 * 针对uint8的数据创建的纹理 只适配uint8的数据
 */

class GPUResource{
  public:
    //todo only one instance for one GPU
    explicit GPUResource(int index);
    ~GPUResource();

    int getGPUIndex() const;



    enum ResourceType:int{
        Buffer = 1,Texture = 2
    };

    struct ResourceDesc{
        ResourceType type;
        size_t size;//in bytes
        int width;
        int pitch;
        int height;
        int depth;
    };

    /**
     * @return true if create GPU resource successfully, false for failed
     */
    bool createGPUResource(ResourceType type,size_t size);



    std::vector<ResourceDesc> getGPUResourceDesc(ResourceType type);

    std::vector<ResourceDesc> getGPUResourceDesc();


    /**
     * @param sync if false must call flush to finish task
     */
    void uploadResource(ResourceType type,PageTable::EntryItem entryItem,void* src,size_t size,bool sync);

    /**
     * @brief upload resource to pos which decided by internal cache policy
     * @return false represent failed to find any valid empty position to load the src data
     */
    bool uploadResource(ResourceType type,void* src,size_t size,bool sync);

    void flush();

    void downloadResource(ResourceType type,PageTable::EntryItem entryItem,void* dst,size_t size,bool sync);

    GPUNode& getGPUNode();

    Renderer* createRenderer(Renderer::Type);

    bool registerRenderer(Renderer*);

    std::vector<Renderer*> getRenderers();

    void removeRenderer(Renderer*);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int gpu_index;
    std::unique_ptr<GPUNode> gpu_node;
};

MRAYNS_END
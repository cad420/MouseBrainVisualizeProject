//
// Created by wyz on 2022/2/24.
//
#pragma once

#include <memory>
#include <vector>
#include "Renderer.hpp"
#include "PageTable.hpp"
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
template <typename T>
class Ref{
  public:
    explicit Ref(T* p):p(p){}
    Ref(const Ref&) = delete;
    Ref(Ref&& rhs):func(std::move(rhs.func)),p(rhs.p){}
    ~Ref(){
        if(func) func();
    }
    template <typename F>
    void bind(F&& f){
        func = [=](){
            f();
        };
    }
  private:
    std::function<void()> func;
    T* p;
};
class GPUResource{
  public:
    //todo only one instance for one GPU
    explicit GPUResource(int index);
    ~GPUResource();

    int getGPUIndex() const;

    static constexpr size_t DefaultGPUMemoryLimitBytes = (size_t)24 << 30;
    static constexpr int DefaultMaxRendererCount = 4;
    struct ResourceLimits{
        size_t max_mem_limit{DefaultGPUMemoryLimitBytes};
        int max_renderer_limit{DefaultMaxRendererCount};
    };

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
    struct ResourceExtent{
        int width;
        int height;
        int depth;
    };

    /**
     * @return true if create GPU resource successfully, false for failed
     */
     //todo create gpu resource at construct
    bool createGPUResource(ResourceDesc desc);



    std::vector<ResourceDesc> getGPUResourceDesc(ResourceType type);

    std::vector<ResourceDesc> getGPUResourceDesc();


    /**
     * @param sync if false must call flush to finish task
     * @return false represent must call flush and then continue to call this
     */
    bool uploadResource(ResourceDesc type,PageTable::EntryItem entryItem,ResourceExtent,void* src,size_t size,bool sync);

    void flush();

    void downloadResource(ResourceDesc type,PageTable::EntryItem entryItem,ResourceExtent,void* dst,size_t size,bool sync);

    PageTable& getPageTable();

    Ref<PageTable> getScopePageTable();

    //must return a available ptr or throw an exception
    Renderer* getRenderer(Renderer::Type);

    void releaseRenderer(Renderer*);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int gpu_index;

    std::unique_ptr<PageTable> page_table;
};

MRAYNS_END
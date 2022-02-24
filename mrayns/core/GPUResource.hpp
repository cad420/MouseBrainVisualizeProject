//
// Created by wyz on 2022/2/24.
//
#pragma once
#include "PageTable.hpp"
#include <memory>
MRAYNS_BEGIN
class GPUResource{
  protected:
    GPUResource() = default;
  public:
    virtual ~GPUResource() = default;

    virtual int getGPUIndex() const = 0;

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
    virtual bool createGPUResource(ResourceType type,size_t size);

    virtual bool getGPUResourceDesc(ResourceType type) const;

    virtual void uploadResource(ResourceType type,PageTable::EntryItem entryItem,void* src,size_t size) = 0;

    virtual void downloadResource(ResourceType type,PageTable::EntryItem entryItem,void* dst,size_t size){}

};

class VulkanGPUResource: public GPUResource{
  public:
    /**
     * @brief Vulkan instance only need create once while VulkanGPUResource may create more than once.
     */
    static std::unique_ptr<VulkanGPUResource> Create(int GPUIndex);

};
MRAYNS_END
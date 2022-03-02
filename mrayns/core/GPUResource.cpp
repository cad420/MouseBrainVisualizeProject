//
// Created by wyz on 2022/2/24.
//
#include "GPUResource.hpp"
#include <cassert>
#include "../common/Logger.hpp"
#include "internal/VulkanUtil.hpp"


MRAYNS_BEGIN



struct GPUResource::Impl{
    std::unique_ptr<internal::VulkanNodeResourceWrapper> node_vulkan_res;

    void createGPUNodeVulkanResource(){

    }

    Impl(){
        auto& instance = internal::VulkanInstance::getInstance();
        auto vk_instance = instance.getVkInstance();
    }

};

GPUResource::GPUResource(int index)
{
    impl = std::make_unique<Impl>();
    gpu_index = index;
    gpu_node = std::make_unique<GPUNode>(index);
}
GPUResource::~GPUResource()
{
}
int GPUResource::getGPUIndex() const
{
    return 0;
}
bool GPUResource::createGPUResource(GPUResource::ResourceType type, size_t size)
{
    return false;
}
std::vector<GPUResource::ResourceDesc> GPUResource::getGPUResourceDesc(GPUResource::ResourceType type)
{
    std::vector<ResourceDesc> desc;
    return desc;
}
std::vector<GPUResource::ResourceDesc> GPUResource::getGPUResourceDesc()
{
    std::vector<ResourceDesc> desc;
    return desc;
}

void GPUResource::uploadResource(
    GPUResource::ResourceType type, PageTable::EntryItem entryItem, void *src, size_t size,bool sync)
{

}
bool GPUResource::uploadResource(GPUResource::ResourceType type, void *src, size_t size, bool sync)
{
    return false;
}
void GPUResource::flush()
{

}
void GPUResource::downloadResource(
    GPUResource::ResourceType type, PageTable::EntryItem entryItem, void *dst,size_t size, bool sync)
{

}
bool GPUResource::registerRenderer(Renderer *)
{
    return false;
}
GPUNode &GPUResource::getGPUNode()
{
    assert(gpu_node.get());
    return *gpu_node;
}
Renderer *GPUResource::createRenderer(Renderer::Type)
{
    return nullptr;
}

MRAYNS_END

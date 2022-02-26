//
// Created by wyz on 2022/2/24.
//
#include "GPUResource.hpp"

MRAYNS_BEGIN
GPUResource::GPUResource(int index)
{

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
GPUResource::ResourceDesc GPUResource::getGPUResourceDesc(GPUResource::ResourceType type) const
{
    return GPUResource::ResourceDesc();
}
void GPUResource::uploadResource(GPUResource::ResourceType type, PageTable::EntryItem entryItem, void *src, size_t size,
                                 bool sync)
{
}
void GPUResource::flush()
{
}
void GPUResource::downloadResource(GPUResource::ResourceType type, PageTable::EntryItem entryItem, void *dst,
                                   size_t size, bool sync)
{
}
bool GPUResource::registerRenderer(Renderer *)
{
    return false;
}

MRAYNS_END

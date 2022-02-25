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

MRAYNS_END

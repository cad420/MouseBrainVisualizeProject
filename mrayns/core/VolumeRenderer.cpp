//
// Created by wyz on 2022/2/25.
//
#include "Renderer.hpp"
MRAYNS_BEGIN

class VolumeRendererImpl: public VolumeRenderer{

};

std::unique_ptr<VolumeRenderer> VolumeRenderer::create(GPUResource &)
{
    return std::unique_ptr<VolumeRenderer>();
}

MRAYNS_END
//
// Created by wyz on 2022/3/3.
//
#include "VulkanVolumeRenderer.hpp"

MRAYNS_BEGIN

namespace internal{



struct VolumeRendererPrivateVulkanResourceWrapper:public VulkanRendererSharedResourceWrapper{
    VkDescriptorSet descriptorSet;
    VkCommandBuffer commandBuffer;

};
struct VulkanVolumeRenderer::Impl{

};
VulkanVolumeRenderer *VulkanVolumeRenderer::Create(VulkanNodeSharedResourceWrapper *)
{
    return nullptr;
}

Renderer::Type VulkanVolumeRenderer::getRendererType() const
{
    return Renderer::VOLUME;
}
const Framebuffer &VulkanVolumeRenderer::getFrameBuffers() const
{
    return {};
}
void VulkanVolumeRenderer::render(const Camera &)
{
}
}



MRAYNS_END
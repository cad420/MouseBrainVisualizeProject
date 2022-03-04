//
// Created by wyz on 2022/3/3.
//
#include "VulkanSliceRenderer.hpp"

MRAYNS_BEGIN

namespace internal{

struct SliceRendererPrivateVulkanResourceWrapper:public VulkanRendererSharedResourceWrapper{
    VkDescriptorSet descriptorSet;
    VkCommandBuffer commandBuffer;

};
struct VulkanSliceRenderer::Impl{

};
Renderer::Type VulkanSliceRenderer::getRendererType() const
{
    return Renderer::VOLUME;
}
const Framebuffer &VulkanSliceRenderer::getFrameBuffers() const
{
    return {};
}
void VulkanSliceRenderer::render(const Slice &)
{
}
VulkanSliceRenderer *VulkanSliceRenderer::Create(VulkanNodeSharedResourceWrapper *)
{
    return nullptr;
}
VulkanSliceRenderer::~VulkanSliceRenderer()
{
}
void VulkanSliceRenderer::destroy()
{
}

}


MRAYNS_END

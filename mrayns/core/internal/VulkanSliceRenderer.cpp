//
// Created by wyz on 2022/3/3.
//
#include "VulkanSliceRenderer.hpp"

MRAYNS_BEGIN

namespace internal{

struct SliceRendererVulkanSharedResourceWrapper:public VulkanRendererResourceWrapper
{
    VkDescriptorSet descriptorSet;
    VkCommandBuffer commandBuffer;

};
struct SliceRendererVulkanPrivateResourceWrapper{

};
struct VulkanSliceRenderer::Impl{

};
Renderer::Type VulkanSliceRenderer::getRendererType() const
{
    return Renderer::SLICE;
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
void VulkanSliceRenderer::setVolume(Volume)
{
}

}


MRAYNS_END

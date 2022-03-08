//
// Created by wyz on 2022/3/3.
//
#include "VulkanVolumeRenderer.hpp"

MRAYNS_BEGIN

namespace internal{



struct VolumeRendererVulkanResourceWrapper:public VulkanRendererSharedResourceWrapper{
    VkDescriptorSet descriptorSet;
    VkCommandBuffer commandBuffer;


};
struct VulkanVolumeRenderer::Impl{
    std::unique_ptr<VolumeRendererVulkanResourceWrapper> renderer_vk_res;


    Impl(){

    }
    ~Impl(){

    }
};
VulkanVolumeRenderer* VulkanVolumeRenderer::Create(VulkanNodeSharedResourceWrapper *node_vk_res)
{

    auto ret = new VulkanVolumeRenderer();

    return ret;
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
VulkanVolumeRenderer::~VulkanVolumeRenderer()
{

}
void VulkanVolumeRenderer::destroy()
{
}
}



MRAYNS_END
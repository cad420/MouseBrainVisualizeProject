//
// Created by wyz on 2022/2/25.
//
#include "Renderer.hpp"
#include "internal/VulkanUtil.hpp"
MRAYNS_BEGIN

struct VolumeRendererPrivateVulkanResourceWrapper{
    VkDescriptorSet descriptorSet;
    VkCommandBuffer commandBuffer;
    VkFramebuffer framebuffer;
};

class VolumeRendererImpl: public VolumeRenderer{

};



MRAYNS_END
//
// Created by wyz on 2022/3/2.
//
#pragma once
#include "../../common/Define.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

#define VK_EXPR(expr)                                                                                                  \
    {                                                                                                                  \
        VkResult res = (expr);                                                                                         \
        if (res != VK_SUCCESS)                                                                                         \
        {                                                                                                              \
            LOG_ERROR("Vulkan error {} in file {} at line {}", res, __FILE__, __LINE__);                               \
        }                                                                                                              \
        assert(res == VK_SUCCESS);                                                                                     \
    }

MRAYNS_BEGIN

namespace internal{


/**
 * 只读的并且是同样的资源是可以共享的
 *
 * 每一个!!!同类型!!!的Renderer应该享有同一个 Pipeline RenderPass DescriptorSetLayout
 * 共享 代理几何体 纹理资源
 * 每一个Renderer都独自拥有各自的DescriptorSets Framebuffer
 */

//using vulkan to decode https://indico.freedesktop.org/event/1/contributions/29/attachments/32/42/xdc2021-gst-vulkan.pdf
//一个GPUResource创建一个physical device和一个logic device
//Renderer
struct VulkanRendererSharedResourceWrapper{
    VkDescriptorSetLayout descriptorSetLayout;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    std::string rendererName;

//    struct{
//        std::vector<Vertex> vertices;
//        std::vector<uint32_t> indices;
//        VkBuffer vertexBuffer;
//        VkDeviceMemory vertexBufferMemory;
//        VkBuffer indexBuffer;
//        VkDeviceMemory indexBufferMemory;
//    } proxyCube;
};
struct VulkanNodeSharedResourceWrapper{
    VkPhysicalDevice physicalDevice;
    VkDevice device; //shared by a GPUResource and multi Renderers

    VkPipelineCache pipelineCache;

    VkQueue graphicsQueue;//can also transfer for tf or other small resource

    VkCommandPool commandPool;

    VkDescriptorPool descriptorPool;

    std::unique_ptr<VulkanRendererSharedResourceWrapper[]> pRendererResWrapper;
    int rendererResCount{0};

};
struct VulkanNodeResourceWrapper:public VulkanNodeSharedResourceWrapper{
    VkQueue transferQueue;//used only for volume block texture transfer

    VkCommandBuffer transferCommandBuffer;

};

class VulkanInstance{
  public:
    static VulkanInstance& getInstance();

    VkInstance getVkInstance();




  private:
    void createInstance();

    VulkanInstance();

    struct Impl;
    std::unique_ptr<Impl> impl;
};


}

MRAYNS_END
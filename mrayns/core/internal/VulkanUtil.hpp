//
// Created by wyz on 2022/3/2.
//
#pragma once
#include "../../common/Define.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>


#include <vk_mem_alloc.h>
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

std::vector<const char*> getValidationLayers();
std::vector<const char*> getRequiredDeviceExtensions();

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
    VkDevice device{VK_NULL_HANDLE}; //shared ptr get by VulkanNodeSharedResourceWrapper
    VkQueue graphicsQueue;//can also transfer for tf or other small resource


    VkDescriptorSetLayout descriptorSetLayout;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkFramebuffer framebuffer;
    VkCommandPool graphicsPool;
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    std::string rendererName;
};
/**
 * https://stackoverflow.com/questions/55272626/what-is-actually-a-queue-family-in-vulkan
 * https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueSubmit.html
 * https://stackoverflow.com/questions/37575012/should-i-try-to-use-as-many-queues-as-possible
 * this is only 3 queue families in gpu rtx3090 for vulkan version 1.3 and nvidia driver version 5.11
 * count = 16 VkQueueFlagBits = 15 = VK_QUEUE_GRAPHICS_BIT + VK_QUEUE_COMPUTE_BIT + VK_QUEUE_TRANSFER_BIT + VK_QUEUE_SPARSE_BINDING_BIT
 * count = 8 VkQueueFlagBits = 14 =                          VK_QUEUE_COMPUTE_BIT + VK_QUEUE_TRANSFER_BIT + VK_QUEUE_SPARSE_BINDING_BIT
 * count = 2 VkQueueFlagBits = 12                                                 + VK_QUEUE_TRANSFER_BIT + VK_QUEUE_SPARSE_BINDING_BIT
 */
struct VulkanNodeSharedResourceWrapper{
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE}; //shared by a GPUResource and multi Renderers

    VmaAllocator allocator;//for large texture

    //command pool can only be used in single-thread context for command buffer alloc, ret or free
    VkCommandPool graphicsCommandPool{VK_NULL_HANDLE};

    static constexpr int maxRendererCount{4};

    uint32_t graphicsQueueFamilyIndex;

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
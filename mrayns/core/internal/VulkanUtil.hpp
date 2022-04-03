//
// Created by wyz on 2022/3/2.
//
#pragma once
#include "../../common/Define.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include "../../geometry/Mesh.hpp"
#include <mutex>
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
struct FramebufferAttachment{
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory mem{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkFormat format{VK_FORMAT_UNDEFINED};
};
//using vulkan to decode https://indico.freedesktop.org/event/1/contributions/29/attachments/32/42/xdc2021-gst-vulkan.pdf
//一个GPUResource创建一个physical device和一个logic device
//Renderer
struct VulkanRendererResourceWrapper
{
    //shared resource
    //device is shared by node
    VkDevice shared_device{VK_NULL_HANDLE}; //shared ptr get by VulkanNodeSharedResourceWrapper
    //shared with the renderer in the same node
    VkQueue shared_graphics_queue;//can also transfer for tf or other small resource

    VkCommandPool shared_graphics_command_pool;
    std::mutex pool_mtx;

    //public resource
    //shared for same type renderer but not shared with each other
    //some static resource for the type of renderer

    inline static int DefaultFrameWidth = 1280;
    inline static int DefaultFrameHeight = 720;


//    VkCommandPool graphicsPool;//should use for transfer page table and proxy cube data

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

    //command pool can only be used in single-thread context for command buffer alloc, ret or free and recording!!!
    VkCommandPool graphicsCommandPool{VK_NULL_HANDLE};

    static constexpr int maxRendererCount{4};

    uint32_t graphicsQueueFamilyIndex;

    //https://www.khronos.org/assets/uploads/developers/library/2018-vulkan-devday/03-Memory.pdf
    //staging buffer VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT will alloc cpu memory
    //VK_FORMAT_R8_UNORM specifies a one-component, 8-bit unsigned normalized format that has a single 8-bit R component
    struct TextureWrapper{
        VkImage image;
#ifdef DEBUG_WINDOW
        VkDeviceMemory mem;//debug
#else
        VmaAllocation allocation;
#endif

        VkImageView view;
        VkExtent3D extent;
    };
    VkSampler texture_sampler{VK_NULL_HANDLE};
    std::vector<TextureWrapper> textures;

};


void SetupVulkanRendererSharedResources(VulkanNodeSharedResourceWrapper* node_vk_res,
                                         VulkanRendererResourceWrapper * renderer_vk_res);

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


VkBool32 getSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat *depthFormat);

uint32_t getMemoryTypeIndex(VkPhysicalDevice physicalDevice,uint32_t typeBits, VkMemoryPropertyFlags properties);

//use default vulkan allocator
void createImage(VkPhysicalDevice physicalDevice,VkDevice device,uint32_t width,uint32_t height,uint32_t mipLevels,
                 VkSampleCountFlagBits numSamples,VkFormat format,
                 VkImageTiling tiling,VkImageUsageFlags usage,VkMemoryPropertyFlags properties,
                 VkImage& image,VkDeviceMemory& imageMemory,VkImageType type = VK_IMAGE_TYPE_2D,uint32_t depth = 1);

void createImageView(VkDevice device,VkImage image,VkFormat format,VkImageAspectFlags aspectFlags,uint32_t mipLevels,VkImageView& imageView,
                     VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D);

void createBuffer(VkPhysicalDevice physicalDevice,VkDevice device,
                  VkDeviceSize size,VkBufferUsageFlags usage,VkMemoryPropertyFlags properties,
                  VkBuffer& buffer,VkDeviceMemory& bufferMemory);

std::vector<char> readShaderFile(const std::string& filename);

VkShaderModule createShaderModule(VkDevice device,const std::vector<char>& code);

using Vertex = ::mrayns::Vertex;

VkVertexInputBindingDescription getVertexBindingDescription();

VkVertexInputAttributeDescription getVertexAttributeDescription();

}

MRAYNS_END
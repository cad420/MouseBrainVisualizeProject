//
// Created by wyz on 2022/2/24.
//
#include "GPUResource.hpp"
#include <cassert>
#include "../common/Logger.hpp"
#define VMA_IMPLEMENTATION
#include "internal/VulkanUtil.hpp"
#include <set>
#include "internal/VulkanVolumeRenderer.hpp"
#include "internal/VulkanSliceRenderer.hpp"
#include <mutex>
#include <condition_variable>

MRAYNS_BEGIN

struct VulkanNodeResourceWrapper:public internal::VulkanNodeSharedResourceWrapper{
    VkQueue transferQueue{VK_NULL_HANDLE};//used only for volume block texture transfer
    uint32_t transferQueueFamilyIndex;
//    VkCommandBuffer transferCommandBuffer{VK_NULL_HANDLE};
    VkCommandPool transferCommandPool{VK_NULL_HANDLE};//only work in single thread context
    std::mutex cmd_pool_mtx;


    //https://www.khronos.org/assets/uploads/developers/library/2018-vulkan-devday/03-Memory.pdf
    //staging buffer VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT will alloc cpu memory
    struct TextureWrapper{
        VkImage image;
        VkDeviceMemory mem;
        VkImageView view;
        VkSampler sampler;
    };
    std::vector<TextureWrapper> textures;
};

struct GPUResource::Impl{
    std::unique_ptr<VulkanNodeResourceWrapper> node_vulkan_res;
    VkInstance vk_instance;//this is a pointer and should be get from global unique vulkan instance
    void destroyGPUNodeVulkanResource(){

    }
    void createGPUNodeVulkanSharedResource(int GPUIndex){
        assert(vk_instance);
        assert(node_vulkan_res.get());
        //one gpu node create one vulkan physical device
        createGPUNodeVulkanSharedPhysicalDevice(GPUIndex);
        createGPUNodeVulkanSharedLogicDevice();
        createGPUNodeVulkanSharedCommandPool();
        createGPUNodeVulkanSharedAllocator();
    }
    //internal
    void createGPUNodeVulkanSharedPhysicalDevice(int GPUIndex){
        uint32_t physicalDeviceCount = 0;
        VK_EXPR(vkEnumeratePhysicalDevices(vk_instance,&physicalDeviceCount,nullptr));
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        VK_EXPR(vkEnumeratePhysicalDevices(vk_instance,&physicalDeviceCount,physicalDevices.data()));
        LOG_INFO("There are total {} GPUs and choose GPU {} to create node resource.",physicalDeviceCount,GPUIndex);
        node_vulkan_res->physicalDevice = physicalDevices[GPUIndex];

        auto requiredDeviceExtensions = internal::getRequiredDeviceExtensions();

        uint32_t availableExtensionCount;
        vkEnumerateDeviceExtensionProperties(node_vulkan_res->physicalDevice,nullptr,
                                             &availableExtensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);
        vkEnumerateDeviceExtensionProperties(node_vulkan_res->physicalDevice,nullptr,
                                             &availableExtensionCount,availableExtensions.data());

        std::set<std::string> requiredExtensions(requiredDeviceExtensions.begin(),
                                                 requiredDeviceExtensions.end());
        for(const auto& extension:availableExtensions){
            requiredExtensions.erase(extension.extensionName);
        }
        if(requiredExtensions.empty()){
            LOG_INFO("Create vulkan physical device for GPU {} successfully",GPUIndex);
        }
        else{
            throw std::runtime_error("GPU "+std::to_string(GPUIndex)+" in used is not all supporting for required device extensions");
        }
    }
    //internal
    //
    void createGPUNodeVulkanSharedLogicDevice(){
        uint32_t queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(node_vulkan_res->physicalDevice,&queueFamilyCount,nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(node_vulkan_res->physicalDevice,&queueFamilyCount,queueFamilyProperties.data());
        VkDeviceQueueCreateInfo transferQueueCreateInfo{};
        const float defaultQueuePriority{0.f};
        for(uint32_t i = 0;i<queueFamilyCount;i++){
            if((queueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
                && !(queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
                && !(queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)){
                node_vulkan_res->transferQueueFamilyIndex = i;
                transferQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                transferQueueCreateInfo.queueFamilyIndex = i;
                transferQueueCreateInfo.queueCount = 1;
                transferQueueCreateInfo.pQueuePriorities = &defaultQueuePriority;
                break;
            }
        }
        const float defaultGraphicsQueuePriority[internal::VulkanNodeSharedResourceWrapper::maxRendererCount]
            = {0.25f,0.25f,0.25f,0.25f};
        VkDeviceQueueCreateInfo graphicsQueueCreateInfo{};
        for(uint32_t i = 0;i<queueFamilyCount;i++){
            if(queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT){
                node_vulkan_res->graphicsQueueFamilyIndex = i;
                graphicsQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                graphicsQueueCreateInfo.queueFamilyIndex = i;
                graphicsQueueCreateInfo.queueCount = node_vulkan_res->maxRendererCount;
                graphicsQueueCreateInfo.pQueuePriorities = defaultGraphicsQueuePriority;
                break;
            }
        }
        auto validationLayers = internal::getValidationLayers();
        auto deviceExtensions = internal::getRequiredDeviceExtensions();

        VkDeviceQueueCreateInfo queueCreateInfos[2]={transferQueueCreateInfo,graphicsQueueCreateInfo};
        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = 2;
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
        deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceCreateInfo.enabledLayerCount = validationLayers.size();
        deviceCreateInfo.ppEnabledLayerNames = validationLayers.data();
//        deviceCreateInfo.pEnabledFeatures;
        VK_EXPR(vkCreateDevice(node_vulkan_res->physicalDevice,&deviceCreateInfo,nullptr,&node_vulkan_res->device));
        LOG_INFO("create gpu node vulkan logic device successfully");
        vkGetDeviceQueue(node_vulkan_res->device,node_vulkan_res->transferQueueFamilyIndex,0,&node_vulkan_res->transferQueue);
        assert(node_vulkan_res->transferQueue);
    }
    void createGPUNodeVulkanSharedCommandPool(){
        VkCommandPoolCreateInfo cmdPoolCreateInfo{};
        cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolCreateInfo.queueFamilyIndex = node_vulkan_res->graphicsQueueFamilyIndex;
//        cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_EXPR(vkCreateCommandPool(node_vulkan_res->device,&cmdPoolCreateInfo,nullptr,&node_vulkan_res->graphicsCommandPool));
        assert(node_vulkan_res->graphicsCommandPool);
        LOG_INFO("create graphics command pool successfully");
    }
    void createGPUNodeVulkanSharedAllocator(){
        VmaAllocatorCreateInfo allocatorCreateInfo{};
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
        allocatorCreateInfo.physicalDevice = node_vulkan_res->physicalDevice;
        allocatorCreateInfo.device = node_vulkan_res->device;
        allocatorCreateInfo.instance = vk_instance;
//        allocatorCreateInfo.flags;
        VK_EXPR(vmaCreateAllocator(&allocatorCreateInfo,&node_vulkan_res->allocator));
        LOG_INFO("create vma alloctor successfully");
    }
    void createGPUNodeVulkanPrivateResource(){
        assert(node_vulkan_res->transferQueue);
        createGPUNodeVulkanPrivateCommandPool();

    }
    void createGPUNodeVulkanPrivateCommandPool(){
        VkCommandPoolCreateInfo cmdPoolInfo{};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = node_vulkan_res->transferQueueFamilyIndex;
        VK_EXPR(vkCreateCommandPool(node_vulkan_res->device,&cmdPoolInfo,nullptr,&node_vulkan_res->transferCommandPool));
        assert(node_vulkan_res->transferCommandPool);
        LOG_INFO("create transfer command pool successfully");
    }



    Impl(int GPUIndex){
        this->vk_instance = internal::VulkanInstance::getInstance().getVkInstance();
        this->node_vulkan_res = std::make_unique<VulkanNodeResourceWrapper>();
        createGPUNodeVulkanSharedResource(GPUIndex);
        createGPUNodeVulkanPrivateResource();
    }
    ~Impl(){
        destroyGPUNodeVulkanResource();
    }

};

GPUResource::GPUResource(int index)
{
    impl = std::make_unique<Impl>(index);
    gpu_index = index;
    gpu_node = std::make_unique<GPUNode>(index);
}
GPUResource::~GPUResource()
{
}
int GPUResource::getGPUIndex() const
{
    return 0;
}
bool GPUResource::createGPUResource(GPUResource::ResourceDesc desc, size_t size)
{
    return false;
}
std::vector<GPUResource::ResourceDesc> GPUResource::getGPUResourceDesc(GPUResource::ResourceType type)
{
    std::vector<ResourceDesc> desc;
    return desc;
}
std::vector<GPUResource::ResourceDesc> GPUResource::getGPUResourceDesc()
{
    std::vector<ResourceDesc> desc;
    return desc;
}

/**
 * 该函数需要处理在多线程的环境下被调用的情况
 * 目前设计 一个线程只分配到一个Renderer 为了尽快使得一个Renderer所需的全部数据都上传完毕
 * 每次上传的资源根据线程id分开记录 即copy command根据线程id分开记录 但是首先都要先从src ptr拷贝到staging buffer
 * staging buffer是只分配CPU内存的 但是也有限制 所以如果staging buffer超过一定数量就返回false
 * 然后需要显式调用flush将当前线程的staging buffer的上传到gpu中 然后释放这部分内存
 */
bool GPUResource::uploadResource(
    GPUResource::ResourceType type, PageTable::EntryItem entryItem, void *src, size_t size,bool sync)
{
    return false;
}

void GPUResource::flush()
{

}
void GPUResource::downloadResource(
    GPUResource::ResourceType type, PageTable::EntryItem entryItem, void *dst,size_t size, bool sync)
{

}
bool GPUResource::registerRenderer(Renderer *)
{
    return false;
}
GPUNode &GPUResource::getGPUNode()
{
    assert(gpu_node.get());
    return *gpu_node;
}
Renderer *GPUResource::createRenderer(Renderer::Type type)
{
    //todo check if number of renderer is reach maximum
    if(type == Renderer::VOLUME){
        return internal::VulkanVolumeRenderer::Create(impl->node_vulkan_res.get());
    }
    else if(type == Renderer::SLICE){
        return internal::VulkanSliceRenderer::Create(impl->node_vulkan_res.get());
    }
    else{
        return nullptr;
    }
}

MRAYNS_END

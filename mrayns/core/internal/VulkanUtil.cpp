//
// Created by wyz on 2022/3/2.
//
#define VMA_IMPLEMENTATION
#include "VulkanUtil.hpp"

#include "../../common/Logger.hpp"
#include <iostream>
#include <fstream>
MRAYNS_BEGIN

namespace internal{

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {

};
const std::vector<const char*> instanceExtensions = {
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessageCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objectType,
    uint64_t object,
    size_t location,
    int32_t messageCode,
    const char* pLayerPrefix,
    const char* pMessage,
    void* pUserData)
{
    LOG_INFO("[VALIDATION]:{} - {}", pLayerPrefix, pMessage);
    return VK_FALSE;
}



bool checkValidationLayerSupport(){
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount,nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount,availableLayers.data());

    LOG_INFO("available validation layer:");
    for(int i=0;i<layerCount;i++){
        LOG_INFO("\t{0}",availableLayers[i].layerName);
    }

    for(const char* layerName:validationLayers){
        bool layerFound = false;
        for(const auto& layerProperties:availableLayers){
            if(strcmp(layerName,layerProperties.layerName) == 0){
                layerFound = true;
                break;
            }
        }
        if(!layerFound){
            return false;
        }
    }
    return true;
}

std::vector<const char*> getRequiredDeviceExtensions(){
    std::vector<const char*> extensions;
    extensions.insert(extensions.end(),deviceExtensions.begin(),deviceExtensions.end());

    //add other extensions here


    return extensions;
}
std::vector<const char *> getValidationLayers()
{
    return validationLayers;
}


void SetupVulkanRendererSharedResources(VulkanNodeSharedResourceWrapper* node_vk_res,
                                         VulkanRendererResourceWrapper * renderer_vk_res){
    assert(node_vk_res && renderer_vk_res);
    renderer_vk_res->shared_device = node_vk_res->device;
    vkGetDeviceQueue(node_vk_res->device,node_vk_res->graphicsQueueFamilyIndex,0,&renderer_vk_res->shared_graphics_queue);
    assert(renderer_vk_res->shared_graphics_queue);
    //todo fix
    renderer_vk_res->shared_graphics_command_pool = node_vk_res->graphicsCommandPool;
}
VkBool32 getSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat *depthFormat)
{
    // Since all depth formats may be optional, we need to find a suitable depth format to use
    // Start with the highest precision packed format
    std::vector<VkFormat> depthFormats = {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (auto& format : depthFormats)
    {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        // Format must support depth stencil attachment for optimal tiling
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            *depthFormat = format;
            return true;
        }
    }

    return false;
}
uint32_t getMemoryTypeIndex(VkPhysicalDevice physicalDevice,uint32_t typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
    for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
        if ((typeBits & 1) == 1) {
            if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        typeBits >>= 1;
    }
    return 0;
}
void createImage(VkPhysicalDevice physicalDevice,VkDevice device,
                 uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
                 VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage &image,
                 VkDeviceMemory &imageMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = numSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.flags = 0;//optional

    VK_EXPR(vkCreateImage(device,&imageInfo, nullptr,&image));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device,image,&memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = getMemoryTypeIndex(physicalDevice,memRequirements.memoryTypeBits,properties);

    if(vkAllocateMemory(device,&allocInfo,nullptr,&imageMemory)!=VK_SUCCESS){
        throw std::runtime_error("failed to allocate image memory!");
    }
    vkBindImageMemory(device,image,imageMemory,0);
}
void createImageView(VkDevice device,VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels,
                     VkImageView &imageView)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if(vkCreateImageView(device,&viewInfo, nullptr,&imageView)!=VK_SUCCESS){
        throw std::runtime_error("failed to create texture image view!");
    }
}
std::vector<char> readShaderFile(const std::string &filename)
{
    std::ifstream file(filename,std::ios::ate|std::ios::binary);

    if(!file.is_open()){
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(),fileSize);

    file.close();

    return buffer;
}
VkShaderModule createShaderModule(VkDevice device,const std::vector<char> &code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if(vkCreateShaderModule(device,&createInfo,nullptr,&shaderModule)!=VK_SUCCESS){
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}
VkVertexInputBindingDescription getVertexBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}
VkVertexInputAttributeDescription getVertexAttributeDescription()
{
    VkVertexInputAttributeDescription attributeDescription{};

    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = offsetof(Vertex,pos);

    return attributeDescription;
}

struct VulkanInstance::Impl{
    VkInstance instance;
//    VkDebugUtilsMessengerEXT debugMessenger;
    VkDebugReportCallbackEXT debugReportCallback{};
    VkInstance getVkInstance(){
        return instance;
    }
    void createInstance(){
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "MouseBrainVisualizeProject";
        appInfo.applicationVersion = VK_MAKE_VERSION(1,0,0);
        appInfo.pEngineName = "mrayns";
        appInfo.engineVersion = VK_MAKE_VERSION(1,0,0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;



        createInfo.enabledExtensionCount = instanceExtensions.size();
        createInfo.ppEnabledExtensionNames = instanceExtensions.data();

        if(enableValidationLayers){
            bool layersAvailable = checkValidationLayerSupport();
            if(!layersAvailable){
                throw std::runtime_error("validation layers requested, but not available!");
            }
            createInfo.ppEnabledLayerNames = validationLayers.data();
            createInfo.enabledLayerCount = validationLayers.size();
        }

        vkCreateInstance(&createInfo,nullptr,&instance);

        assert(instance);

        LOG_INFO("create vulkan instance successfully");

        if(enableValidationLayers){
            createDebugReportCallback();
        }
    }
    void createDebugReportCallback(){
        VkDebugReportCallbackCreateInfoEXT debugReportCreateInfo = {};
        debugReportCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugReportCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        debugReportCreateInfo.pfnCallback = (PFN_vkDebugReportCallbackEXT)debugMessageCallback;

        // We have to explicitly load this function.
        PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
        assert(vkCreateDebugReportCallbackEXT);
        VK_EXPR(vkCreateDebugReportCallbackEXT(instance, &debugReportCreateInfo, nullptr, &debugReportCallback));
        assert(debugReportCallback);
        LOG_INFO("create debug report callback successfully");
    }
};


VulkanInstance &VulkanInstance::getInstance()
{
    static VulkanInstance vk_instance;
    return vk_instance;
}

VkInstance VulkanInstance::getVkInstance()
{
    return impl->getVkInstance();
}
void VulkanInstance::createInstance()
{
    //init vulkan instance
    impl->createInstance();

}
VulkanInstance::VulkanInstance()
{
    impl = std::make_unique<Impl>();
    createInstance();
}

}

MRAYNS_END
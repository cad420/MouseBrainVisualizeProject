//
// Created by wyz on 2022/3/2.
//
#include "VulkanUtil.hpp"
#include "../../common/Logger.hpp"
#include <iostream>
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
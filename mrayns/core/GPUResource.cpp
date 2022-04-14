//
// Created by wyz on 2022/2/24.
//
#include "GPUResource.hpp"
#include <cassert>
#include "../common/Logger.hpp"
#include "../utils/Timer.hpp"
#include "internal/VulkanUtil.hpp"
#include <set>
#include "internal/VulkanVolumeRenderer.hpp"
#include "internal/VulkanSliceRenderer.hpp"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
MRAYNS_BEGIN

struct VulkanNodeResourceWrapper:public internal::VulkanNodeSharedResourceWrapper{
    VkQueue transferQueue{VK_NULL_HANDLE};//used only for volume block texture transfer
    uint32_t transferQueueFamilyIndex;
//    VkCommandBuffer transferCommandBuffer{VK_NULL_HANDLE};
    VkCommandPool transferCommandPool{VK_NULL_HANDLE};//only work in single thread context
    std::mutex cmd_pool_mtx;
    std::mutex transfer_mtx;

};

namespace internal
{
class VulkanRendererDeleter
{
  public:
    void operator()(Renderer *renderer) const noexcept
    {
        if (renderer->getRendererType() == Renderer::VOLUME)
        {
            dynamic_cast<VulkanVolumeRenderer *>(renderer)->destroy();
        }
        else if (renderer->getRendererType() == Renderer::SLICE)
        {
            dynamic_cast<VulkanSliceRenderer *>(renderer)->destroy();
        }
    }
};
}

struct GPUResource::Impl{
    struct : public ResourceLimits{
        size_t used_mem_bytes{0};
        int renderer_count{0};
    }limit;
    const size_t EleSize = 1;
    std::unique_ptr<VulkanNodeResourceWrapper> node_vulkan_res;
    VkInstance vk_instance;//this is a pointer and should be get from global unique vulkan instance
    struct StagingBuffer{
        VkBuffer buffer;
#ifdef DEBUG_WINDOW
        VkDeviceMemory mem;
#else
        VmaAllocation allocation;
        VmaAllocationInfo allocInfo;
#endif
    };
    //https://stackoverflow.com/questions/31112852/how-stdunordered-map-is-implemented
    //unordered_map is thread-safe even each thread write different key for value
    std::unordered_map<size_t,std::vector<VkCommandBuffer>> thread_cmd;
    std::unordered_map<size_t,std::vector<StagingBuffer>> thread_staging_buffers;
    std::mutex thread_cmd_mtx;

    using RendererPtr = std::unique_ptr<Renderer,internal::VulkanRendererDeleter>;
    std::vector<RendererPtr> renderers;
    std::mutex renderer_mtx;
    std::queue<Renderer*> available_renderers;
    std::condition_variable renderer_cv;

    bool _createRenderer(Renderer::Type type){
//        std::lock_guard<std::mutex> lk(renderer_mtx);
        if(limit.renderer_count < limit.max_renderer_limit){
            RendererPtr renderer;
            if(type == Renderer::VOLUME){
                renderer = RendererPtr(internal::VulkanVolumeRenderer::Create(node_vulkan_res.get()));
            }
            else if(type == Renderer::SLICE){
                renderer = RendererPtr(internal::VulkanSliceRenderer::Create(node_vulkan_res.get()));
            }
            else{
                LOG_ERROR("renderer type not supported!");
                return false;
            }
            if(!renderer) return false;
            limit.renderer_count++;
            bool wasEmpty = available_renderers.empty();
            available_renderers.push(renderer.get());
            renderers.emplace_back(std::move(renderer));
            if(wasEmpty && !available_renderers.empty()){
                renderer_cv.notify_one();
            }
            return true;
        }
        else{
            return false;
        }
    }
    bool destroyOneRenderer(Renderer::Type type){
        return false;
    }
    //internal
    void destroyAllRenderer(){
        std::lock_guard<std::mutex> lk(renderer_mtx);
        while(!available_renderers.empty()) available_renderers.pop();
        renderers.clear();
    }
    Renderer* getRenderer(Renderer::Type type){

        std::unique_lock<std::mutex> lk(renderer_mtx);
        bool exist = false;
        for(auto& p:renderers){
            if(p->getRendererType()==type){
                exist = true;
                break;
            }
        }
        if(!exist){
            bool e = _createRenderer(type);
            if(!e){
                return nullptr;
            }
            else{
                lk.unlock();
                return getRenderer(type);
            }
        }
        //check if there is an available renderer
        int n = available_renderers.size();
        while(n--){
            auto renderer = available_renderers.front();
            available_renderers.pop();
            if(renderer->getRendererType() == type){
                return renderer;
            }
            else{
                available_renderers.push(renderer);
            }
        }
        bool e = _createRenderer(type);
        if(!e){
            return nullptr;
        }
        else{
            //wait will unlock the mutex and when notified will lock the mutex again
            renderer_cv.wait(lk,[this](){
              return !available_renderers.empty();
            });
        }
        lk.unlock();
        return getRenderer(type);
    }
    bool releaseRenderer(Renderer* renderer){
        std::unique_lock<std::mutex> lk(renderer_mtx);
        for(const auto& p:renderers){
            if(p.get() == renderer){
                bool wasEmpty = available_renderers.empty();
                available_renderers.push(renderer);
                if(wasEmpty && !available_renderers.empty()){
                    lk.unlock();
                    renderer_cv.notify_one();
                    return true;
                }
            }
        }
        return false;
    }

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
        createGPUNodeVulkanSharedSampler();

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
        LOG_INFO("create vma allocator successfully");
    }
    void createGPUNodeVulkanSharedSampler(){
        VkSamplerCreateInfo samplerCreateInfo{};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerCreateInfo.anisotropyEnable = VK_FALSE;
        samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

        VK_EXPR(vkCreateSampler(node_vulkan_res->device,&samplerCreateInfo,nullptr,&node_vulkan_res->texture_sampler));
        LOG_INFO("create texture sampler successfully");
    }
    int createGPUNodeVulkanSharedTextureResource(uint32_t width,uint32_t height,uint32_t depth){
        size_t create_size = EleSize * width * height * depth;
        if(limit.used_mem_bytes + create_size >= limit.max_mem_limit){
            LOG_INFO("GPUResource memory not enough to create new texture resource");
            return -1;
        }
        VulkanNodeResourceWrapper::TextureWrapper texture{};
        texture.extent = {width,height,depth};
        VkImageCreateInfo imageCreateInfo{};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
        imageCreateInfo.extent = {width,height,depth};
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.format = VK_FORMAT_R8_UNORM;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;//VK_SHARING_MODE_EXCLUSIVE;//???
        imageCreateInfo.queueFamilyIndexCount = 2;
        uint32_t queueFamilyIndices[2] = {node_vulkan_res->transferQueueFamilyIndex,node_vulkan_res->graphicsQueueFamilyIndex};
        imageCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        //sb!!! buffer alloc by vma can't use in RenderDoc

#ifdef DEBUG_WINDOW
        internal::createImage(node_vulkan_res->physicalDevice,node_vulkan_res->device,width,height,1,VK_SAMPLE_COUNT_1_BIT,
                              VK_FORMAT_R8_UNORM,VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,texture.image,texture.mem,VK_IMAGE_TYPE_3D,depth);
#else
        auto res = vmaCreateImage(node_vulkan_res->allocator,&imageCreateInfo,&allocInfo,&texture.image,&texture.allocation,nullptr);
        if(res !=VK_SUCCESS){
            LOG_INFO("vma alloc texture memory failed");
            return -1;
        }
        else{
            LOG_INFO("vma alloc texture memory successfully");
        }
#endif
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = texture.image;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewCreateInfo.format = VK_FORMAT_R8_UNORM;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;
        VK_EXPR(vkCreateImageView(node_vulkan_res->device,&viewCreateInfo,nullptr,&texture.view));
        LOG_INFO("create texture view successfully");

        auto transitionCmd = beginSingleTimeCommands();

        //纹理的layout使用了 VK_IMAGE_LAYOUT_GENERAL
        //因为纹理的更新和读操作可能会同时进行 即在对纹理部分区域进行更新时 渲染器可能在读纹理用于渲染
        //todo
        //but!!! VK_IMAGE_LAYOUT_GENERAL 并不适合采样 所以 还是要设置为VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 但是每次上传数据的时候重新更换layout??? 会不会多线程冲突

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(transitionCmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,0,nullptr,
                             0,nullptr,1,&barrier);

        endSingleTimeCommands(transitionCmd);
        node_vulkan_res->textures.emplace_back(texture);
        return node_vulkan_res->textures.size()-1;
    }

    VkCommandBuffer beginSingleTimeCommands(){
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = node_vulkan_res->transferCommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;

        VK_EXPR(vkAllocateCommandBuffers(node_vulkan_res->device, &allocInfo, &commandBuffer));

        assert(commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_EXPR(vkBeginCommandBuffer(commandBuffer,&beginInfo));

        return commandBuffer;
    }
    void endSingleTimeCommands(VkCommandBuffer commandBuffer){
        VK_EXPR(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VK_EXPR(vkQueueSubmit(node_vulkan_res->transferQueue,1,&submitInfo,VK_NULL_HANDLE));
        VK_EXPR(vkQueueWaitIdle(node_vulkan_res->transferQueue));

        {
            std::lock_guard<std::mutex> lk(node_vulkan_res->cmd_pool_mtx);
            vkFreeCommandBuffers(node_vulkan_res->device, node_vulkan_res->transferCommandPool, 1, &commandBuffer);
        }
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

    //internal
    bool createStagingBuffer(StagingBuffer& stagingBuffer,size_t size){
        //add a lock
        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size  = size;
        bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT  |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
        START_TIMER
#ifdef DEBUG_WINDOW
        internal::createBuffer(node_vulkan_res->physicalDevice,node_vulkan_res->device,bufferCreateInfo.size,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                               stagingBuffer.buffer,stagingBuffer.mem);
        auto res = VK_SUCCESS;
#else
        auto res = vmaCreateBuffer(node_vulkan_res->allocator,&bufferCreateInfo,&allocInfo,&stagingBuffer.buffer,&stagingBuffer.allocation,&stagingBuffer.allocInfo);
#endif
        STOP_TIMER("create staging buffer cost")
        if(res == VK_SUCCESS){
            LOG_INFO("create staging buffer for size({}) successfully",size);
            return true;
        }
        else{
            LOG_ERROR("create staging buffer for size({}) failed",size);
            return false;
        }

    }
    //internal
    void destroyStagingBuffer(StagingBuffer& stagingBuffer){
#ifdef DEBUG_WINDOW

#else
        vmaDestroyBuffer(node_vulkan_res->allocator,stagingBuffer.buffer,stagingBuffer.allocation);
#endif
    }

    //no need for threadID and it will immediately upload data from cpu to gpu
    bool updateTextureSubImage3DSync(int texID,int srcX,int srcY,int srcZ,uint32_t lenX,uint32_t lenY,uint32_t lenZ,void *data){
        if(texID<0 || texID>=node_vulkan_res->textures.size()){
            LOG_ERROR("texID out of range");
            return false;
        }
        auto tex = node_vulkan_res->textures[texID];

        size_t size = (size_t)lenX * lenY * lenZ;
        StagingBuffer stagingBuffer{};
        auto alloc = createStagingBuffer(stagingBuffer,size);
        if(!alloc) return false;

        //copy data to staging buffer
        copyToStagingBuffer(stagingBuffer,data,size);

        std::lock_guard<std::mutex> lk(node_vulkan_res->transfer_mtx);
        auto transferCommand = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageOffset = {srcX,srcY,srcZ};
        region.imageExtent = {lenX,lenY,lenZ};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        vkCmdCopyBufferToImage(transferCommand,stagingBuffer.buffer,tex.image,VK_IMAGE_LAYOUT_GENERAL,1,&region);

        endSingleTimeCommands(transferCommand);

        destroyStagingBuffer(stagingBuffer);

        return true;
    }
    bool updateTextureSubImage3DAsync(size_t threadID,int texID,int srcX,int srcY,int srcZ,uint32_t lenX,uint32_t lenY,uint32_t lenZ,void *data){
        if(texID<0 || texID>=node_vulkan_res->textures.size()){
            LOG_ERROR("texID out of range");
            return false;
        }
        auto tex = node_vulkan_res->textures[texID];

        size_t size = (size_t)lenX * lenY * lenZ;
        StagingBuffer stagingBuffer{};
        auto alloc = createStagingBuffer(stagingBuffer,size);
        if(!alloc) return false;

        copyToStagingBuffer(stagingBuffer,data,size);


        std::lock_guard<std::mutex> lk(node_vulkan_res->transfer_mtx);
        auto asyncTransferCommand = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageOffset = {srcX,srcY,srcZ};
        region.imageExtent = {lenX,lenY,lenZ};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;


        vkCmdCopyBufferToImage(asyncTransferCommand,stagingBuffer.buffer,tex.image,VK_IMAGE_LAYOUT_GENERAL,1,&region);

        VK_EXPR(vkEndCommandBuffer(asyncTransferCommand));

        {
            std::lock_guard<std::mutex> lk(thread_cmd_mtx);
            thread_cmd[threadID].emplace_back(asyncTransferCommand);
            thread_staging_buffers[threadID].emplace_back(stagingBuffer);
        }
        return true;
    }
    void copyToStagingBuffer(const StagingBuffer& stagingBuffer,void* data,size_t size){
//        void* p;
//        VK_EXPR(vmaMapMemory(node_vulkan_res->allocator,stagingBuffer.allocation,&p));
        START_TIMER
#ifdef DEBUG_WINDOW
        void* p;
        VK_EXPR(vkMapMemory(node_vulkan_res->device,stagingBuffer.mem,0,size,0,&p));
        memcpy(p,data,size);
#else
        memcpy(stagingBuffer.allocInfo.pMappedData,data,size);//copy to host mem need time more than normal cpu memcpy
#endif
        STOP_TIMER("copy to staging buffer")
//        vmaUnmapMemory(node_vulkan_res->allocator,stagingBuffer.allocation);
#ifdef DEBUG_WINDOW
        vkUnmapMemory(node_vulkan_res->device,stagingBuffer.mem);
#endif
    }
    void flushStagingBufferAndRelease(size_t threadID){
//        START_TIMER
        std::vector<VkCommandBuffer> cmd;
        VkSubmitInfo submitInfo{};
        {
            std::lock_guard<std::mutex> lk(thread_cmd_mtx);
            if (thread_cmd[threadID].empty())
                return;
            cmd = std::move(thread_cmd[threadID]);

            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = cmd.size();
            submitInfo.pCommandBuffers = cmd.data();
        }

        {
            std::lock_guard<std::mutex> lk(node_vulkan_res->transfer_mtx);
            vkQueueSubmit(node_vulkan_res->transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(node_vulkan_res->transferQueue);
        }

        {
            std::lock_guard<std::mutex> lk(node_vulkan_res->cmd_pool_mtx);
            for(auto& commandBuffer:cmd){
                vkFreeCommandBuffers(node_vulkan_res->device, node_vulkan_res->transferCommandPool, 1, &commandBuffer);
            }
        }
        {
            std::lock_guard<std::mutex> lk(thread_cmd_mtx);
            for(auto& stagingBuffer : thread_staging_buffers[threadID]){
                destroyStagingBuffer(stagingBuffer);
            }
            thread_staging_buffers[threadID].clear();
        }
//        STOP_TIMER("flush staging buffer")
//        LOG_DEBUG("flush staging buffer to gpu texture");
    }
    //internal
    void flushStagingBufferAndRelease(){
        for(auto& item:thread_cmd){
            if(!item.second.empty()){
                flushStagingBufferAndRelease(item.first);
            }
        }
    }


    Impl(int GPUIndex){
        this->vk_instance = internal::VulkanInstance::getInstance().getVkInstance();
        this->node_vulkan_res = std::make_unique<VulkanNodeResourceWrapper>();
        createGPUNodeVulkanSharedResource(GPUIndex);
        createGPUNodeVulkanPrivateResource();
        //test
//        createGPUNodeVulkanSharedTextureResource(1024,1024,1024);
    }
    ~Impl(){
        destroyGPUNodeVulkanResource();
    }

};

static void ExtendPageTable(PageTable& pageTable,int w,const GPUResource::ResourceDesc& desc){
    try{
        int xx = desc.width / desc.block_length;
        int yy = desc.height / desc.block_length;
        int zz = desc.depth / desc.block_length;
        for(int z = 0; z < zz; z++){
            for(int y = 0; y < yy; y++){
                for(int x = 0; x < xx; x++){
                    pageTable.insert(PageTable::EntryItem{x,y,z,w});
                }
            }
        }
    }
    catch (const std::exception& err)
    {
        LOG_ERROR("ExtendPageTable error: {}",err.what());
    }
}

GPUResource::GPUResource(int index)
{
    impl = std::make_unique<Impl>(index);
    gpu_index = index;
    page_table = std::make_unique<PageTable>();
}

GPUResource::~GPUResource()
{
}

int GPUResource::getGPUIndex() const
{
    return gpu_index;
}

bool GPUResource::createGPUResource(GPUResource::ResourceDesc desc)
{
    //just handle uint8 texture creation
    if(desc.type != Texture) return false;
    auto res = impl->createGPUNodeVulkanSharedTextureResource(desc.width,desc.height,desc.depth);
    //todo update page table status and gpu node
    if(res == -1) return false;

    ExtendPageTable(getPageTable(),res,desc);

    return true;
}

std::vector<GPUResource::ResourceDesc> GPUResource::getGPUResourceDesc(GPUResource::ResourceType type)
{
    //todo
    std::vector<ResourceDesc> desc;
    return desc;
}

std::vector<GPUResource::ResourceDesc> GPUResource::getGPUResourceDesc()
{
    //todo
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
    GPUResource::ResourceDesc desc, PageTable::EntryItem entryItem, ResourceExtent extent,void *src, size_t size,bool sync)
{
    if(desc.type!=Texture) return false;

    auto tid = desc.id;
    LOG_INFO("GPUResource::uploadResource called thread id: {}",tid);

    auto extent_size = size_t(extent.width) * extent.height * extent.depth;
    assert(extent_size == size);
    int texID = entryItem.w;
    int srcX = entryItem.x * desc.width;
    int srcY = entryItem.y * desc.height;
    int srcZ = entryItem.z * desc.depth;

    if(sync){
        return impl->updateTextureSubImage3DSync(texID,srcX,srcY,srcZ,extent.width,extent.height,extent.depth,src);
    }
    else{
        return impl->updateTextureSubImage3DAsync(
            tid,texID,srcX,srcY,srcZ,extent.width,extent.height,extent.depth,src);
    }
}

void GPUResource::flush(size_t tid)
{
    impl->flushStagingBufferAndRelease(tid);
}

void GPUResource::downloadResource(
    GPUResource::ResourceDesc desc, PageTable::EntryItem entryItem, ResourceExtent,void *dst,size_t size, bool sync)
{

}

Renderer *GPUResource::getRenderer(Renderer::Type type)
{
    auto renderer = impl->getRenderer(type);
    if(!renderer){
        LOG_ERROR("can't get this type of renderer");
    }
    return renderer;

}

void GPUResource::releaseRenderer(Renderer *renderer)
{
    auto res = impl->releaseRenderer(renderer);
    if(!res){
        LOG_ERROR("release an invalid renderer");
        throw std::runtime_error("release an invalid renderer");
    }
}

PageTable& GPUResource::getPageTable()
{

    assert(page_table.get());
    return *page_table;


}
Ref<PageTable> GPUResource::getScopePageTable()
{
//    page_table->lock();
//    auto f = std::bind(&PageTable::unlock,page_table.get());
    Ref<PageTable> res(page_table.get());
//    res.bind(std::move(f));
    return std::move(res);
}

MRAYNS_END

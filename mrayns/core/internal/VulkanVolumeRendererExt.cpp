//
// Created by wyz on 2022/4/15.
//
#include "VulkanVolumeRendererExt.hpp"
#include "../../common/Logger.hpp"
#include "../../algorithm/ColorMapping.hpp"
#include "../../geometry/Mesh.hpp"
#include "../../utils/Timer.hpp"
#include "../GPUResource.hpp"
#include <array>
#include "../../Config.hpp"
#include "../../algorithm/GeometryHelper.hpp"
#include "Common.hpp"

MRAYNS_BEGIN
namespace internal{


struct VolumeRendererExtVulkanSharedResourceWrapper: public VulkanRendererResourceWrapper{
    VkDescriptorSetLayout rayPosLayout{};
    VkDescriptorSetLayout rayCastLayout{};

    VkRenderPass firstRenderPass{};//begin a new frame
    VkRenderPass secondRenderPass{};//continue render a frame

    struct{
        VkPipelineLayout rayPos;
        VkPipelineLayout rayCast;
    }pipelineLayout{};

    struct{
        VkPipeline rayPos;
        VkPipeline rayCast;
    }pipeline{};

    static constexpr VkFormat rayEntryImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    static constexpr VkFormat rayExitImageFormat  = VK_FORMAT_R32G32B32A32_SFLOAT;
    static constexpr VkFormat depthImageFormat    = VK_FORMAT_D32_SFLOAT_S8_UINT;
    static constexpr VkFormat colorImageFormat    = VK_FORMAT_R8G8B8A8_UNORM;
};

struct VolumeRendererExtVulkanPrivateResourceWrapper{
    struct {
        VkDescriptorSet raypos;
        VkDescriptorSet raycast;
    }descriptorSet;

    FramebufferAttachment rayEntryAttachment;
    FramebufferAttachment rayExitAttachment;

    FramebufferAttachment depthAttachment;

    FramebufferAttachment colorAttachment;

    struct{
        VkBuffer buffer;
#ifdef DEBUG_WINDOW
        VkDeviceMemory mem;
#else
        VmaAllocation allocation;
#endif
    }result_color;

    struct{
        VkImage image;
        VkImageView view;
        VkDeviceMemory mem;
        VkSampler sampler;
    }tf;

    struct UBO{
        VkBuffer buffer;
#ifdef DEBUG_WINDOW
        VkDeviceMemory mem;
#else
        VmaAllocation allocation;
#endif
    };
    struct SSBO{
        VkBuffer buffer;
#ifdef DEBUG_WINDOW
        VkDeviceMemory mem;
#else
        VmaAllocation allocation;
#endif
        void* mapped_ptr;
        size_t size;
    };

    UBO mvpMatrixUBO;
    UBO viewPosUBO;

    UBO volumeInfoUBO;
    UBO renderInfoUBO;

    SSBO renderPassTagSSBO;

    VkFramebuffer firstFramebuffer;
    VkFramebuffer secondFramebuffer;

    VkCommandBuffer firstDrawCommand;
    VkCommandBuffer secondDrawCommand;
    VkCommandBuffer resultCopyCommand;

    VmaAllocator allocator;

    UBO pageTableUBO;

    struct{
        VkBuffer vertexBuffer;
#ifdef DEBUG_WINDOW
        VkDeviceMemory vertexMem;
#else
        VmaAllocation vertexAllocation;
#endif
        VkBuffer indexBuffer;
#ifdef DEBUG_WINDOW
        VkDeviceMemory indexMem;
#else
        VmaAllocation indexAllocation;
#endif
    }proxyCubeBuffer;
};

struct VulkanVolumeRendererExt::Impl{
    std::unique_ptr<VolumeRendererExtVulkanPrivateResourceWrapper> renderer_vk_res;
    VolumeRendererExtVulkanSharedResourceWrapper* shared_renderer_vk_res;
    VulkanNodeSharedResourceWrapper* node_vk_res;
    Framebuffer render_result;
    Volume volume;
    struct{
        Matrix4f mvp = Matrix4f(1.f);
        Matrix4f model = Matrix4f(1.f);
    }matrix_transform;
    Vector4f view_pos;
    struct RenderPassTag{
        uint32_t finished; uint32_t padding[3];
    }renderPass_tag;
    static constexpr int MaxVolumeLod = 12;
    struct VolumeInfo{
        Vector4ui volume_dim;//x y z max_lod
        Vector3ui lod0_block_dim;uint32_t padding0 = 1;
        Vector3f volume_space;uint32_t padding1 = 2;
        Vector3f inv_volume_space;uint32_t padding2 = 3;
        Vector3f virtual_block_length_space;
        uint32_t virtual_block_length;
        uint32_t padding;
        uint32_t padding_block_length;
        float voxel;
        uint32_t padding3 = 4;
        Vector4f inv_texture_shape[GPUResource::DefaultMaxGPUTextureCount] = {Vector4f{0.f}};
    } volume_info;
    void* result_color_mapped_ptr = nullptr;
    using HashTable = ::mrayns::internal::MappingTable::HashTable;
    HashTable page_table;
    struct RenderInfo{
        Vector3f view_pos;
        float ray_dist;
        float ray_step;
        uint32_t padding0[3]={1,2,3};
        Vector4f lod_dist[MaxVolumeLod];
    } render_info;
    struct ProxyCube{
        Vertex vertices[8] = {};//set by volume
        static constexpr uint32_t indices[36] = {
            0,1,2, 0,2,3,
            2,6,7, 2,7,3,
            1,5,6, 1,6,2,
            0,4,5, 0,5,1,
            3,7,4, 3,4,0,
            6,5,4, 6,4,7
        };
    } proxy_cube;

#ifdef DEBUG_WINDOW
    struct Debug{
        int window_w,window_h;
        GLFWwindow* window;
        VkSurfaceKHR surface;
        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;//color depth
        VkExtent2D swapChainExtent;//resolution fo images in swap chain
        std::vector<VkImageView> swapChainImageViews;
        VulkanNodeSharedResourceWrapper* node_vk_res;
        void init(){
            //glfw
            glfwInit();
            glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);

            window = glfwCreateWindow(window_w,window_h,"DebugWindow",nullptr,nullptr);

            createSurface();
            createSwapChain();
        }
        void createSurface(){
            if(glfwCreateWindowSurface(VulkanInstance::getInstance().getVkInstance(),
                                 window,nullptr,&surface)!=VK_SUCCESS){
                throw std::runtime_error("failed to create window surface");
            }
        }
        void createSwapChain(){
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(node_vk_res->physicalDevice,0,surface,&presentSupport);
            assert(presentSupport);
            uint32_t formatCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(node_vk_res->physicalDevice,surface,&formatCount, nullptr);
            std::vector<VkSurfaceFormatKHR> formats;
            if(formatCount!=0){
                formats.resize(formatCount);
                vkGetPhysicalDeviceSurfaceFormatsKHR(node_vk_res->physicalDevice,surface,&formatCount,formats.data());
            }
            uint32_t presentModeCount;
            vkGetPhysicalDeviceSurfacePresentModesKHR(node_vk_res->physicalDevice,surface,&presentModeCount, nullptr);
            std::vector<VkPresentModeKHR> presentModes;
            if(presentModeCount!=0){
                presentModes.resize(presentModeCount);
                vkGetPhysicalDeviceSurfacePresentModesKHR(node_vk_res->physicalDevice,surface,&presentModeCount,
                                                          presentModes.data());
            }

            VkSurfaceFormatKHR surfaceFormat{VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
            VkPresentModeKHR presentMode{VK_PRESENT_MODE_MAILBOX_KHR};
            swapChainExtent.width = window_w;
            swapChainExtent.height = window_h;

            uint32_t imageCount = 2;

            VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
            createInfo.surface =surface;
            createInfo.minImageCount = imageCount;
            createInfo.imageFormat = surfaceFormat.format;
            createInfo.imageColorSpace = surfaceFormat.colorSpace;
            createInfo.imageExtent = swapChainExtent;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

            createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
            createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            createInfo.presentMode = presentMode;
            createInfo.clipped = VK_TRUE;
            createInfo.oldSwapchain = VK_NULL_HANDLE;

            VK_EXPR(vkCreateSwapchainKHR(node_vk_res->device,&createInfo,nullptr,&swapChain));

            vkGetSwapchainImagesKHR(node_vk_res->device,swapChain,&imageCount,nullptr);
            swapChainImages.resize(imageCount);
            vkGetSwapchainImagesKHR(node_vk_res->device,swapChain,&imageCount,swapChainImages.data());
            swapChainImageFormat = surfaceFormat.format;
            swapChainImageViews.resize(imageCount);
            for(int i = 0; i< imageCount; i++){
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = swapChainImages[i];
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = swapChainImageFormat;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = 0;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;
                VK_EXPR(vkCreateImageView(node_vk_res->device,&viewInfo,nullptr,&swapChainImageViews[i]));

            }

        }
        void submit(VkQueue queue){
            uint32_t imageIndex;
            VkResult result = vkAcquireNextImageKHR(node_vk_res->device,swapChain,UINT64_MAX,VK_NULL_HANDLE,
                                                    VK_NULL_HANDLE,&imageIndex);

            if(result == VK_ERROR_OUT_OF_DATE_KHR){
                return;
            }
            else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){
                throw std::runtime_error("failed to acquire swap chain image!");
            }

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;


            VkSwapchainKHR swapChains[] = {swapChain};
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = swapChains;

            presentInfo.pImageIndices = &imageIndex;

            result = vkQueuePresentKHR(queue,&presentInfo);

            if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR){

            }
            else if(result != VK_SUCCESS){
                throw std::runtime_error("failed to present swap chain image!");
            }
        }

    }debug;
#endif


    void destroy(){

    }
    void setVolume(const Volume& volume){
        if(!volume.isValid()){
            LOG_ERROR("invalid volume");
            return;
        }
        this->volume = volume;

        updateVolumeInfo();

        updateProxyCube();
    }
    void setTransferFunction1D(const float* data,int dim = 256,int length = 1024){
        assert(dim == 256 && length == 1024);
        //create staging buffer
        VkBuffer stagingBuffer;
        VmaAllocation allocation;
        VkDeviceMemory mem;
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.size = sizeof(float)*length;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
#ifdef DEBUG_WINDOW
        internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                               stagingBuffer,mem);
#else
        VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocationInfo,&stagingBuffer,&allocation,nullptr));
#endif
        //copy src buffer to staging buffer
        void* p;
#ifdef DEBUG_WINDOW
        vkMapMemory(node_vk_res->device,mem,0,bufferInfo.size,0,&p);
#else
        VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,allocation,&p));
#endif
        memcpy(p,data,sizeof(float)*length);
#ifdef DEBUG_WINDOW
        vkUnmapMemory(node_vk_res->device,mem);
#else
        vmaUnmapMemory(renderer_vk_res->allocator,allocation);
#endif
        //copy staging buffer to device
        VkCommandBuffer commandBuffer = beginSingleTimeCommand();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0,0,0};
        region.imageExtent = {(uint32_t)256,1,1};

        vkCmdCopyBufferToImage(commandBuffer,stagingBuffer,renderer_vk_res->tf.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);

        endSingleTimeCommand(commandBuffer);
        //finally transition image layout
        transitionImageLayout(renderer_vk_res->tf.image,VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#ifdef DEBUG_WINDOW

#else
        vmaDestroyBuffer(renderer_vk_res->allocator,stagingBuffer,allocation);
#endif
        LOG_INFO("successfully upload transfer function");
    }
    void updatePageTable(const std::vector<PageTableItem>& items){
        page_table.clear();
        for(const auto& item:items){
            page_table.append({item.second,item.first});
        }

        uploadPageTable();
    }
    bool renderPass(const VolumeRendererCamera& camera,bool newFrame){
        //submit draw command
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        if(newFrame){
            updateRenderParamsUBO(camera);
            submitInfo.pCommandBuffers = &renderer_vk_res->firstDrawCommand;
        }
        else{
            submitInfo.pCommandBuffers = &renderer_vk_res->secondDrawCommand;
        }

        {
            std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
            VK_EXPR(vkCreateFence(shared_renderer_vk_res->shared_device, &fenceInfo, nullptr, &fence));
            VK_EXPR(vkQueueSubmit(shared_renderer_vk_res->shared_graphics_queue,1,&submitInfo,fence));
        }
        VK_EXPR(vkWaitForFences(shared_renderer_vk_res->shared_device,1,&fence,VK_TRUE,UINT64_MAX));
        {
            std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
            vkDestroyFence(shared_renderer_vk_res->shared_device, fence, nullptr);
        }
        //new frame should call once more again
        if(newFrame){
            return renderPass(camera,false);
        }
        //get render pass tag, required host coherent
        auto tag = reinterpret_cast<uint32_t*>(renderer_vk_res->renderPassTagSSBO.mapped_ptr)[0];
        return tag == 0;
    }
    const Framebuffer& getFrameRenderResult(){
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &renderer_vk_res->resultCopyCommand;
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        {
            std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
            VK_EXPR(vkCreateFence(shared_renderer_vk_res->shared_device, &fenceInfo, nullptr, &fence));
            VK_EXPR(vkQueueSubmit(shared_renderer_vk_res->shared_graphics_queue, 1, &submitInfo, fence));
        }
        VK_EXPR(vkWaitForFences(shared_renderer_vk_res->shared_device,1,&fence,VK_TRUE,UINT64_MAX));
        {
            std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
            vkDestroyFence(shared_renderer_vk_res->shared_device, fence, nullptr);
        }

        ::memcpy(render_result.getColors().data(),result_color_mapped_ptr,render_result.getColors().size());

        return render_result;
    }

    void updateRenderParamsUBO(const VolumeRendererCamera& camera){
        //params for ray pos
        view_pos = vec4(camera.position,Contain(BoundBox{{0.f,0.f,0.f},{volume.getVolumeSpace() * volume.getVolumeDim()}},camera.position));
        matrix_transform.model = Matrix4f(1.f);
        auto view = GeometryHelper::ExtractViewMatrixFromCamera(camera);
        //todo
        auto proj = perspective(radians(camera.fov*0.5f),static_cast<float>(camera.width)/static_cast<float>(camera.height),0.1f,20.f);
        matrix_transform.mvp = proj * view * matrix_transform.model;
        uploadRayPosUBO();

        render_info.ray_step = camera.raycasting_step;
        render_info.ray_dist = camera.raycasting_max_dist;
        render_info.view_pos = camera.position;
        for(int i = 0; i < camera.lod_dist.MaxLod;i++){
            render_info.lod_dist[i] = Vector4f(camera.lod_dist.lod_dist[i]);
        }
        uploadRenderParamsUBO();
    }
    void uploadRayPosUBO(){
        //ray pos
        {
            void *data;
            size_t size = sizeof(matrix_transform);
#ifdef DEBUG_WINDOW
            vkMapMemory(shared_renderer_vk_res->shared_device, renderer_vk_res->mvpMatrixUBO.mem, 0, size, 0, &data);
            memcpy(data, &matrix_transform, size);
            vkUnmapMemory(shared_renderer_vk_res->shared_device, renderer_vk_res->mvpMatrixUBO.mem);
#else
            VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->mvpMatrixUBO.allocation,&data));
            memcpy(data,&matrix_transform,size);
            vmaUnmapMemory(renderer_vk_res->allocator,renderer_vk_res->mvpMatrixUBO.allocation);
#endif
        }
        //ray cast
        {
            void *data;
            size_t size = sizeof(view_pos);
#ifdef DEBUG_WINDOW
            vkMapMemory(shared_renderer_vk_res->shared_device, renderer_vk_res->viewPosUBO.mem, 0, size, 0, &data);
            memcpy(data, &view_pos, size);
            vkUnmapMemory(shared_renderer_vk_res->shared_device, renderer_vk_res->viewPosUBO.mem);
#else
            VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->viewPosUBO.allocation,&data));
            memcpy(data,&view_pos,size);
            vmaUnmapMemory(renderer_vk_res->allocator,renderer_vk_res->viewPosUBO.allocation);
#endif
        }
    }
    void uploadRenderParamsUBO(){
        void* data;
        size_t size = sizeof(RenderInfo);
#ifdef DEBUG_WINDOW
        vkMapMemory(shared_renderer_vk_res->shared_device,renderer_vk_res->renderInfoUBO.mem,0,size,0,&data);
#else
        VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->renderInfoUBO.allocation,&data));
#endif
        memcpy(data,&render_info,size);
#ifdef DEBUG_WINDOW
        vkUnmapMemory(shared_renderer_vk_res->shared_device,renderer_vk_res->renderInfoUBO.mem);
#else
        vmaUnmapMemory(renderer_vk_res->allocator,renderer_vk_res->renderInfoUBO.allocation);
#endif
    }
    void updateVolumeInfo(){
        assert(volume.isValid());
        volume.getVolumeDim(reinterpret_cast<int &>(volume_info.volume_dim.x),
                            reinterpret_cast<int &>(volume_info.volume_dim.y),
                            reinterpret_cast<int &>(volume_info.volume_dim.z));
        volume_info.volume_dim.w = volume.getMaxLod();
        volume_info.padding_block_length = volume.getBlockLength();
        volume_info.padding = volume.getBlockPadding();

        volume_info.virtual_block_length = volume_info.padding_block_length - volume_info.padding * 2;

        volume_info.lod0_block_dim = (volume_info.volume_dim + volume_info.virtual_block_length - (uint32_t)1) / volume_info.virtual_block_length;
        volume.getVolumeSpace(volume_info.volume_space.x, volume_info.volume_space.y, volume_info.volume_space.z);
        volume_info.inv_volume_space = 1.f / volume_info.volume_space;
        volume_info.virtual_block_length_space = (float)volume_info.virtual_block_length * volume_info.volume_space;

        volume_info.voxel = std::min({volume_info.volume_space.x, volume_info.volume_space.y, volume_info.volume_space.z});

        //upload
        uploadVolumeInfoUBO();
    }
    void uploadVolumeInfoUBO(){
        void* data;
        size_t size = sizeof(VolumeInfo);
#ifdef DEBUG_WINDOW
        vkMapMemory(shared_renderer_vk_res->shared_device,renderer_vk_res->volumeInfoUBO.mem,0,size,0,&data);
#else
        VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->volumeInfoUBO.allocation,&data));
#endif
        memcpy(data,&volume_info,size);
#ifdef DEBUG_WINDOW
        vkUnmapMemory(shared_renderer_vk_res->shared_device,renderer_vk_res->volumeInfoUBO.mem);
#else
        vmaUnmapMemory(renderer_vk_res->allocator,renderer_vk_res->volumeInfoUBO.allocation);
#endif
    }
    void updateProxyCube(){
        //must after update volume
        float space_x,space_y,space_z;
        int volume_dim_x,volume_dim_y,volume_dim_z;
        volume.getVolumeSpace(space_x,space_y,space_z);
        volume.getVolumeDim(volume_dim_x,volume_dim_y,volume_dim_z);
        auto volume_board_x = volume_dim_x * space_x;
        auto volume_board_y = volume_dim_y * space_y;
        auto volume_board_z = volume_dim_z * space_z;
        proxy_cube.vertices[0].pos = {0.f,0.f,0.f};
        proxy_cube.vertices[1].pos = {volume_board_x,0.f,0.f};
        proxy_cube.vertices[2].pos = {volume_board_x,volume_board_y,0.f};
        proxy_cube.vertices[3].pos = {0.f,volume_board_y,0.f};
        proxy_cube.vertices[4].pos = {0.f,0.f,volume_board_z};
        proxy_cube.vertices[5].pos = {volume_board_x,0.f,volume_board_z};
        proxy_cube.vertices[6].pos = {volume_board_x,volume_board_y,volume_board_z};
        proxy_cube.vertices[7].pos = {0.f,volume_board_y,volume_board_z};
        //upload
        uploadProxyCubeBuffer();
    }
    void uploadProxyCubeBuffer(){
        VkBuffer stagingBuffer;
        VmaAllocation allocation;
        VkDeviceMemory mem;
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.size = sizeof(ProxyCube::vertices);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
#ifdef DEBUG_WINDOW
        internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                               stagingBuffer,mem);
#else
        VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocationInfo,&stagingBuffer,&allocation,nullptr));
#endif
        //copy src buffer to staging buffer
        void* p;
#ifdef DEBUG_WINDOW
        vkMapMemory(node_vk_res->device,mem,0,bufferInfo.size,0,&p);
#else
        VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,allocation,&p));
#endif
        memcpy(p, proxy_cube.vertices,sizeof(ProxyCube::vertices));
#ifdef DEBUG_WINDOW
        vkUnmapMemory(node_vk_res->device,mem);
#else
        vmaUnmapMemory(renderer_vk_res->allocator,allocation);
#endif
        //copy staging buffer to device
        VkCommandBuffer commandBuffer = beginSingleTimeCommand();

        VkBufferCopy copyRegion{};
        copyRegion.size = sizeof(ProxyCube::vertices);
        vkCmdCopyBuffer(commandBuffer,stagingBuffer,renderer_vk_res->proxyCubeBuffer.vertexBuffer,1,&copyRegion);

        endSingleTimeCommand(commandBuffer);

        //upload indices
        {
            VkBuffer stagingBuffer;
            VmaAllocation allocation;
            VkDeviceMemory mem;
            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.size = sizeof(ProxyCube::indices);
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo allocationInfo{};
            allocationInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                   stagingBuffer,mem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocationInfo,&stagingBuffer,&allocation,nullptr));
#endif
            //copy src buffer to staging buffer
            void* p;
#ifdef DEBUG_WINDOW
            vkMapMemory(node_vk_res->device,mem,0,bufferInfo.size,0,&p);
#else
            VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,allocation,&p));
#endif
            memcpy(p, proxy_cube.indices,sizeof(ProxyCube::indices));
#ifdef DEBUG_WINDOW
            vkUnmapMemory(node_vk_res->device,mem);
#else
            vmaUnmapMemory(renderer_vk_res->allocator,allocation);
#endif
            //copy staging buffer to device
            VkCommandBuffer commandBuffer = beginSingleTimeCommand();

            VkBufferCopy copyRegion{};
            copyRegion.size = sizeof(ProxyCube::indices);
            vkCmdCopyBuffer(commandBuffer,stagingBuffer,renderer_vk_res->proxyCubeBuffer.indexBuffer,1,&copyRegion);
            endSingleTimeCommand(commandBuffer);
        }

//        vmaDestroyBuffer(renderer_vk_res->allocator,stagingBuffer,allocation);
    }
    void uploadPageTable(){
        void* data;
#ifdef DEBUG_WINDOW
        vkMapMemory(shared_renderer_vk_res->shared_device,renderer_vk_res->pageTableUBO.mem,0,sizeof(HashTable),0,&data);
#else
        VK_EXPR(vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->pageTableUBO.allocation,&data));
#endif
        memcpy(data,&page_table,sizeof(page_table));
#ifdef DEBUG_WINDOW
        vkUnmapMemory(shared_renderer_vk_res->shared_device,renderer_vk_res->pageTableUBO.mem);
#else
        vmaUnmapMemory(renderer_vk_res->allocator,renderer_vk_res->pageTableUBO.allocation);
#endif
    }

    VkCommandBuffer beginSingleTimeCommand(){
        assert(shared_renderer_vk_res);
        std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = shared_renderer_vk_res->shared_graphics_command_pool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(shared_renderer_vk_res->shared_device,&allocInfo,&commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer,&beginInfo);

        return commandBuffer;
    }
    void endSingleTimeCommand(VkCommandBuffer commandBuffer){
        assert(shared_renderer_vk_res);
        std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(shared_renderer_vk_res->shared_graphics_queue,1,&submitInfo,VK_NULL_HANDLE);
        vkQueueWaitIdle(shared_renderer_vk_res->shared_graphics_queue);

        vkFreeCommandBuffers(shared_renderer_vk_res->shared_device,
                             shared_renderer_vk_res->shared_graphics_command_pool,
                             1,&commandBuffer);
    }

    void transitionImageLayout(VkImage image, VkFormat, VkImageLayout oldLayout,
                               VkImageLayout newLayout){
        VkCommandBuffer commandBuffer = beginSingleTimeCommand();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL){
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else{
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(commandBuffer,sourceStage,destinationStage,0,
                             0, nullptr,
                             0, nullptr,
                             1,&barrier);

        endSingleTimeCommand(commandBuffer);
    }

    void initResources(
        VolumeRendererExtVulkanSharedResourceWrapper* render_vk_shared_res,
        VulkanNodeSharedResourceWrapper* node_vk_res){
        assert(renderer_vk_res.get() && render_vk_shared_res && node_vk_res);

        this->shared_renderer_vk_res = render_vk_shared_res;
        this->node_vk_res = node_vk_res;

        auto physical_device = node_vk_res->physicalDevice;
        assert(physical_device);
        auto device = render_vk_shared_res->shared_device;
        auto width = VolumeRendererExtVulkanSharedResourceWrapper::DefaultFrameWidth;
        auto height = VolumeRendererExtVulkanSharedResourceWrapper::DefaultFrameHeight;
        int max_renderer_num = GPUResource::DefaultMaxRendererCount;

        //set texture shape
        {
            const auto& texes = node_vk_res->textures;
            for(int i = 0;i<texes.size();i++){
                volume_info.inv_texture_shape[i] = {1.f/texes[i].extent.width,
                                                    1.f/texes[i].extent.height,1.f/texes[i].extent.depth,i};
            }
        }
        //create depth resource
        {
            auto find = getSupportedDepthFormat(physical_device,&renderer_vk_res->depthAttachment.format);
            if(!find){
                throw std::runtime_error("no valid depth format");
            }
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->depthAttachment.format,VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->depthAttachment.image,renderer_vk_res->depthAttachment.mem);

            createImageView(device,renderer_vk_res->depthAttachment.image,renderer_vk_res->depthAttachment.format,
                            VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,1,renderer_vk_res->depthAttachment.view);
        }
        //create color resource
        {
            //ray entry
            renderer_vk_res->rayEntryAttachment.format = VolumeRendererExtVulkanSharedResourceWrapper::rayEntryImageFormat;
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->rayEntryAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->rayEntryAttachment.image,renderer_vk_res->rayEntryAttachment.mem);
            createImageView(device,renderer_vk_res->rayEntryAttachment.image,renderer_vk_res->rayEntryAttachment.format,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->rayEntryAttachment.view);
            //ray exit
            renderer_vk_res->rayExitAttachment.format = VolumeRendererExtVulkanSharedResourceWrapper::rayExitImageFormat;
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->rayExitAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->rayExitAttachment.image,renderer_vk_res->rayExitAttachment.mem);
            createImageView(device,renderer_vk_res->rayExitAttachment.image,renderer_vk_res->rayExitAttachment.format,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->rayExitAttachment.view);
            //color
            renderer_vk_res->colorAttachment.format = VolumeRendererExtVulkanSharedResourceWrapper::colorImageFormat;
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->colorAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_STORAGE_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->colorAttachment.image,renderer_vk_res->colorAttachment.mem);
            createImageView(device,renderer_vk_res->colorAttachment.image,renderer_vk_res->colorAttachment.format,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->colorAttachment.view);
        }
        //debug
#ifdef DEBUG_WINDOW
        {
            debug.window_w = width;
            debug.window_h = height;
            debug.node_vk_res = node_vk_res;
            debug.init();
        }
#endif
        //create framebuffer
        {
            //ray pos
            {
                std::array<VkImageView, 4> attachments = {renderer_vk_res->rayEntryAttachment.view,
                                                          renderer_vk_res->rayExitAttachment.view,
#ifdef DEBUG_WINDOW
                                                          debug.swapChainImageViews[0], // debug
#else
                                                          renderer_vk_res->colorAttachment.view,
#endif
                                                          renderer_vk_res->depthAttachment.view};
                VkFramebufferCreateInfo framebufferInfo{};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = render_vk_shared_res->firstRenderPass;
                framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
                framebufferInfo.pAttachments = attachments.data();
                framebufferInfo.width = width;
                framebufferInfo.height = height;
                framebufferInfo.layers = 1;
                VK_EXPR(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &renderer_vk_res->firstFramebuffer));
            }
            //ray cast
            {
                std::array<VkImageView,1> attachments = {
                    renderer_vk_res->depthAttachment.view
                };
                VkFramebufferCreateInfo framebufferInfo{};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = render_vk_shared_res->secondRenderPass;
                framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
                framebufferInfo.pAttachments = attachments.data();
                framebufferInfo.width = width;
                framebufferInfo.height = height;
                framebufferInfo.layers = 1;
                VK_EXPR(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &renderer_vk_res->secondFramebuffer));
            }
        }
        //create tf texture
        {
            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_1D;
            imageInfo.extent = {256,1,1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = 0;

            createImage(node_vk_res->physicalDevice,render_vk_shared_res->shared_device,
                        256,1,1,VK_SAMPLE_COUNT_1_BIT,VK_FORMAT_R32G32B32A32_SFLOAT,VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->tf.image,renderer_vk_res->tf.mem,VK_IMAGE_TYPE_1D);
            createImageView(render_vk_shared_res->shared_device,renderer_vk_res->tf.image,VK_FORMAT_R32G32B32A32_SFLOAT,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->tf.view,VK_IMAGE_VIEW_TYPE_1D);

            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable = VK_FALSE;//disable
            samplerInfo.maxAnisotropy = 0.f;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable = VK_FALSE;
            samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.minLod = 0;
            samplerInfo.maxLod = 1;
            samplerInfo.mipLodBias = 0;

            VK_EXPR(vkCreateSampler(render_vk_shared_res->shared_device,&samplerInfo,nullptr,&renderer_vk_res->tf.sampler));
            // transition image layout
            transitionImageLayout(renderer_vk_res->tf.image,VK_FORMAT_R32G32B32A32_SFLOAT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        //create vma allocator
        {
            VmaAllocatorCreateInfo allocatorCreateInfo{};
            allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
            allocatorCreateInfo.physicalDevice = node_vk_res->physicalDevice;
            allocatorCreateInfo.device = render_vk_shared_res->shared_device;
            allocatorCreateInfo.instance = VulkanInstance::getInstance().getVkInstance();
//        allocatorCreateInfo.flags;
            VK_EXPR(vmaCreateAllocator(&allocatorCreateInfo,&renderer_vk_res->allocator));
        }
        //create shader ubo
        {
            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size = sizeof(matrix_transform);
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

            //ray pos
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,renderer_vk_res->mvpMatrixUBO.buffer,renderer_vk_res->mvpMatrixUBO.mem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->mvpMatrixUBO.buffer,&renderer_vk_res->mvpMatrixUBO.allocation,nullptr));
#endif

            bufferInfo.size = sizeof(view_pos);
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,renderer_vk_res->viewPosUBO.buffer,renderer_vk_res->viewPosUBO.mem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->viewPosUBO.buffer,&renderer_vk_res->viewPosUBO.allocation,nullptr));
#endif
            //ray cast

            bufferInfo.size = sizeof(VolumeInfo);
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,renderer_vk_res->volumeInfoUBO.buffer,renderer_vk_res->volumeInfoUBO.mem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->volumeInfoUBO.buffer,&renderer_vk_res->volumeInfoUBO.allocation,nullptr));
#endif
            bufferInfo.size = sizeof(RenderInfo);
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,renderer_vk_res->renderInfoUBO.buffer,renderer_vk_res->renderInfoUBO.mem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->renderInfoUBO.buffer,&renderer_vk_res->renderInfoUBO.allocation,nullptr));
#endif
            bufferInfo.size = sizeof(page_table);
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,renderer_vk_res->pageTableUBO.buffer,renderer_vk_res->pageTableUBO.mem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->pageTableUBO.buffer,&renderer_vk_res->pageTableUBO.allocation,nullptr));
#endif
            bufferInfo.size = sizeof(RenderPassTag);
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VmaAllocationInfo info{};
#ifdef DEBUG_WINDOW
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->renderPassTagSSBO.buffer,&renderer_vk_res->renderPassTagSSBO.allocation,&info));
#endif
            renderer_vk_res->renderPassTagSSBO.mapped_ptr = info.pMappedData;
            renderer_vk_res->renderPassTagSSBO.size = bufferInfo.size;
        }
        //create descriptor sets
        {
            //ray pos
            {
                VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocateInfo.descriptorPool = render_vk_shared_res->descriptorPool;
                allocateInfo.descriptorSetCount = 1;
                allocateInfo.pSetLayouts = &render_vk_shared_res->rayPosLayout;
                VK_EXPR(vkAllocateDescriptorSets(render_vk_shared_res->shared_device, &allocateInfo,
                                                 &renderer_vk_res->descriptorSet.raypos));

                VkDescriptorBufferInfo mvpBufferInfo{};
                mvpBufferInfo.buffer = renderer_vk_res->mvpMatrixUBO.buffer;
                mvpBufferInfo.offset = 0;
                mvpBufferInfo.range = sizeof(matrix_transform);

                VkDescriptorBufferInfo viewPosBufferInfo{};
                viewPosBufferInfo.buffer = renderer_vk_res->viewPosUBO.buffer;
                viewPosBufferInfo.offset = 0;
                viewPosBufferInfo.range = sizeof(glm::vec4);

                std::array<VkWriteDescriptorSet,2> descriptorWrites{};
                descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[0].dstSet = renderer_vk_res->descriptorSet.raypos;
                descriptorWrites[0].dstBinding = 0;
                descriptorWrites[0].dstArrayElement = 0;
                descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[0].descriptorCount = 1;
                descriptorWrites[0].pBufferInfo = &mvpBufferInfo;

                descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[1].dstSet = renderer_vk_res->descriptorSet.raypos;
                descriptorWrites[1].dstBinding = 1;
                descriptorWrites[1].dstArrayElement = 0;
                descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[1].descriptorCount = 1;
                descriptorWrites[1].pBufferInfo = &viewPosBufferInfo;

                vkUpdateDescriptorSets(shared_renderer_vk_res->shared_device,descriptorWrites.size(),descriptorWrites.data(),0,nullptr);
            }
            //ray cast
            {
                VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocateInfo.descriptorPool = render_vk_shared_res->descriptorPool;
                allocateInfo.descriptorSetCount = 1;
                allocateInfo.pSetLayouts = &render_vk_shared_res->rayCastLayout;
                VK_EXPR(vkAllocateDescriptorSets(render_vk_shared_res->shared_device,&allocateInfo,&renderer_vk_res->descriptorSet.raycast));

                VkDescriptorImageInfo rayEntryImageInfo{};
                rayEntryImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                rayEntryImageInfo.imageView = renderer_vk_res->rayEntryAttachment.view;
                rayEntryImageInfo.sampler = VK_NULL_HANDLE;

                VkDescriptorImageInfo rayExitImageInfo{};
                rayExitImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                rayExitImageInfo.imageView = renderer_vk_res->rayExitAttachment.view;
                rayExitImageInfo.sampler = VK_NULL_HANDLE;

                VkDescriptorImageInfo interColorImageInfo{};
                interColorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                interColorImageInfo.imageView = renderer_vk_res->colorAttachment.view;
                interColorImageInfo.sampler = VK_NULL_HANDLE;

                VkDescriptorBufferInfo renderPassTagBufferInfo{};
                renderPassTagBufferInfo.buffer = renderer_vk_res->renderPassTagSSBO.buffer;
                renderPassTagBufferInfo.offset = 0;
                renderPassTagBufferInfo.range = renderer_vk_res->renderPassTagSSBO.size;

                VkDescriptorImageInfo tfImageInfo{};
                tfImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                tfImageInfo.imageView = renderer_vk_res->tf.view;
                tfImageInfo.sampler = renderer_vk_res->tf.sampler;

                auto count = node_vk_res->textures.size();
                std::vector<VkDescriptorImageInfo> volumeImageInfo(count);
                for(int i = 0;i<volumeImageInfo.size();i++){
                    volumeImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    volumeImageInfo[i].imageView = node_vk_res->textures[i].view;
                    volumeImageInfo[i].sampler = node_vk_res->texture_sampler;
                }

                VkDescriptorBufferInfo volumeBufferInfo;
                volumeBufferInfo.buffer = renderer_vk_res->volumeInfoUBO.buffer;
                volumeBufferInfo.offset = 0;
                volumeBufferInfo.range = sizeof(VolumeInfo);

                VkDescriptorBufferInfo pageTableBufferInfo{};
                pageTableBufferInfo.buffer = renderer_vk_res->pageTableUBO.buffer;
                pageTableBufferInfo.offset = 0;
                pageTableBufferInfo.range = sizeof(page_table);

                VkDescriptorBufferInfo renderBufferInfo{};
                renderBufferInfo.buffer = renderer_vk_res->renderInfoUBO.buffer;
                renderBufferInfo.offset = 0;
                renderBufferInfo.range = sizeof(RenderInfo);

                std::array<VkWriteDescriptorSet,9> descriptorWrites{};
                descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[0].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[0].dstBinding = 0;
                descriptorWrites[0].dstArrayElement = 0;
                descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                descriptorWrites[0].descriptorCount = 1;
                descriptorWrites[0].pImageInfo = &rayEntryImageInfo;

                descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[1].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[1].dstBinding = 1;
                descriptorWrites[1].dstArrayElement = 0;
                descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                descriptorWrites[1].descriptorCount = 1;
                descriptorWrites[1].pImageInfo = &rayExitImageInfo;

                descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[2].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[2].dstBinding = 2;
                descriptorWrites[2].dstArrayElement = 0;
                descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                descriptorWrites[2].descriptorCount = 1;
                descriptorWrites[2].pImageInfo = &interColorImageInfo;

                descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[3].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[3].dstBinding = 3;
                descriptorWrites[3].dstArrayElement = 0;
                descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[3].descriptorCount = 1;
                descriptorWrites[3].pBufferInfo = &renderPassTagBufferInfo;

                descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[4].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[4].dstBinding = 4;
                descriptorWrites[4].dstArrayElement = 0;
                descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[4].descriptorCount = 1;
                descriptorWrites[4].pImageInfo = &tfImageInfo;

                descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[5].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[5].dstBinding = 5;
                descriptorWrites[5].dstArrayElement = 0;
                descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[5].descriptorCount = volumeImageInfo.size();
                descriptorWrites[5].pImageInfo = volumeImageInfo.data();

                descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[6].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[6].dstBinding = 6;
                descriptorWrites[6].dstArrayElement = 0;
                descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[6].descriptorCount = 1;
                descriptorWrites[6].pBufferInfo = &volumeBufferInfo;

                descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[7].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[7].dstBinding = 7;
                descriptorWrites[7].dstArrayElement = 0;
                descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[7].descriptorCount = 1;
                descriptorWrites[7].pBufferInfo = &pageTableBufferInfo;

                descriptorWrites[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[8].dstSet = renderer_vk_res->descriptorSet.raycast;
                descriptorWrites[8].dstBinding = 8;
                descriptorWrites[8].dstArrayElement = 0;
                descriptorWrites[8].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[8].descriptorCount = 1;
                descriptorWrites[8].pBufferInfo = &renderBufferInfo;

                vkUpdateDescriptorSets(device,descriptorWrites.size(),descriptorWrites.data(),0,nullptr);
            }
        }
        //create proxy cube buffer
        {

            VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferInfo.size = sizeof(ProxyCube::vertices);
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,bufferInfo.usage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,renderer_vk_res->proxyCubeBuffer.vertexBuffer,renderer_vk_res->proxyCubeBuffer.vertexMem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->proxyCubeBuffer.vertexBuffer,&renderer_vk_res->proxyCubeBuffer.vertexAllocation,nullptr));
#endif
            bufferInfo.size = sizeof(ProxyCube::indices);
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
#ifdef DEBUG_WINDOW
            internal::createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferInfo.size,bufferInfo.usage,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,renderer_vk_res->proxyCubeBuffer.indexBuffer,renderer_vk_res->proxyCubeBuffer.indexMem);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->proxyCubeBuffer.indexBuffer,&renderer_vk_res->proxyCubeBuffer.indexAllocation,nullptr));
#endif
        }
        //create result color
        {
            render_result = Framebuffer(width,height);
        }
        //create draw command buffer and record
        {
            //ray pos
            {
                std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
                VkCommandBufferAllocateInfo allocateInfo{};
                allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocateInfo.commandPool = shared_renderer_vk_res->shared_graphics_command_pool;
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = 1;
                VK_EXPR(vkAllocateCommandBuffers(shared_renderer_vk_res->shared_device,
                                                 &allocateInfo,&renderer_vk_res->firstDrawCommand));
                VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                VK_EXPR(vkBeginCommandBuffer(renderer_vk_res->firstDrawCommand,&beginInfo));
                VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                renderPassInfo.renderPass = shared_renderer_vk_res->firstRenderPass;
                renderPassInfo.framebuffer = renderer_vk_res->firstFramebuffer;
                renderPassInfo.renderArea = {0,0};
                renderPassInfo.renderArea.extent = {(uint32_t)width,(uint32_t)height};

                std::array<VkClearValue,4> clearValues{};
                clearValues[0].color = {0.f,0.f,0.f,0.f};
                clearValues[1].color = {0.f,0.f,0.f,0.f};
                clearValues[2].color = {0.f,0.f,0.f,0.f};
                clearValues[3].depthStencil = {1.f,0};
                renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                renderPassInfo.pClearValues = clearValues.data();

                auto cmd = renderer_vk_res->firstDrawCommand;
                vkCmdBeginRenderPass(cmd,&renderPassInfo,VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shared_renderer_vk_res->pipeline.rayPos);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shared_renderer_vk_res->pipelineLayout.rayPos,0,1,
                                        &renderer_vk_res->descriptorSet.raypos,0,nullptr);
                VkDeviceSize offsets[]={0};
                vkCmdBindVertexBuffers(cmd,0,1,&renderer_vk_res->proxyCubeBuffer.vertexBuffer,offsets);
                vkCmdBindIndexBuffer(cmd,renderer_vk_res->proxyCubeBuffer.indexBuffer,0,VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd,36,1,0,0,0);

                vkCmdEndRenderPass(cmd);
                VK_EXPR(vkEndCommandBuffer(cmd));
            }
            //ray cast
            {
                std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
                VkCommandBufferAllocateInfo allocateInfo{};
                allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocateInfo.commandPool = shared_renderer_vk_res->shared_graphics_command_pool;
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocateInfo.commandBufferCount = 1;
                VK_EXPR(vkAllocateCommandBuffers(shared_renderer_vk_res->shared_device,
                                                 &allocateInfo,&renderer_vk_res->secondDrawCommand));
                VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                VK_EXPR(vkBeginCommandBuffer(renderer_vk_res->secondDrawCommand,&beginInfo));
                VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                renderPassInfo.renderPass = shared_renderer_vk_res->secondRenderPass;
                renderPassInfo.framebuffer = renderer_vk_res->secondFramebuffer;
                renderPassInfo.renderArea = {0,0};
                renderPassInfo.renderArea.extent = {(uint32_t)width,(uint32_t)height};

                std::array<VkClearValue,4> clearValues{};
                clearValues[0].color = {0.f,0.f,0.f,0.f};
                clearValues[1].color = {0.f,0.f,0.f,0.f};
                clearValues[2].color = {0.f,0.f,0.f,0.f};
                clearValues[3].depthStencil = {1.f,0};
                renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                renderPassInfo.pClearValues = clearValues.data();

                auto cmd = renderer_vk_res->secondDrawCommand;
                vkCmdBeginRenderPass(cmd,&renderPassInfo,VK_SUBPASS_CONTENTS_INLINE);

                vkCmdNextSubpass(cmd,VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,render_vk_shared_res->pipeline.rayCast);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shared_renderer_vk_res->pipelineLayout.rayCast,0,1,
                                        &renderer_vk_res->descriptorSet.raycast,0,nullptr);
                vkCmdDraw(cmd,6,1,0,0);

                vkCmdEndRenderPass(cmd);
                VK_EXPR(vkEndCommandBuffer(cmd));
            }
        }
        //result download
        {
            VkBufferCreateInfo bufferCreateInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufferCreateInfo.size = render_result.getColors().size();
            bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
#ifdef DEBUG_WINDOW
            createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferCreateInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         renderer_vk_res->result_color.buffer,renderer_vk_res->result_color.mem);
#else
            vmaCreateBuffer(renderer_vk_res->allocator,&bufferCreateInfo,&allocInfo,&renderer_vk_res->result_color.buffer,&renderer_vk_res->result_color.allocation,nullptr);
#endif

            VkCommandBufferAllocateInfo cmdBufAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufAllocInfo.commandBufferCount = 1;
            cmdBufAllocInfo.commandPool = render_vk_shared_res->shared_graphics_command_pool;
            vkAllocateCommandBuffers(device,&cmdBufAllocInfo,&renderer_vk_res->resultCopyCommand);
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_EXPR(vkBeginCommandBuffer(renderer_vk_res->resultCopyCommand,&beginInfo));
            VkBufferImageCopy bufImgCopy{};
            bufImgCopy.bufferOffset = 0;
            bufImgCopy.bufferRowLength = render_result.getColors().width();
            bufImgCopy.bufferImageHeight = render_result.getColors().height();
            bufImgCopy.imageOffset = {0,0,0};
            bufImgCopy.imageExtent = {(uint32_t)width,(uint32_t)height,1};
            bufImgCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            vkCmdCopyImageToBuffer(renderer_vk_res->resultCopyCommand,renderer_vk_res->colorAttachment.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,renderer_vk_res->result_color.buffer,1,&bufImgCopy);
            VK_EXPR(vkEndCommandBuffer(renderer_vk_res->resultCopyCommand));
#ifdef DEBUG_WINDOW
            vkMapMemory(render_vk_shared_res->shared_device,renderer_vk_res->result_color.mem,0,render_result.getColors().size(),0,&result_color_mapped_ptr);
#else
            vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->result_color.allocation,&result_color_mapped_ptr);
#endif
        }
    }
};

static void CreateVulkanVolumeRendererExtSharedResources(
    VolumeRendererExtVulkanSharedResourceWrapper* renderer_vk_res,
    VulkanNodeSharedResourceWrapper* node_vk_res
    ){
    assert(node_vk_res && renderer_vk_res);
    auto physical_device = node_vk_res->physicalDevice;
    assert(physical_device);
    auto device = renderer_vk_res->shared_device;
    auto width = VolumeRendererExtVulkanSharedResourceWrapper::DefaultFrameWidth;
    auto height = VolumeRendererExtVulkanSharedResourceWrapper::DefaultFrameHeight;
    int max_renderer_num = GPUResource::DefaultMaxRendererCount;

    //create render pass
    {
        std::array<VkAttachmentDescription,4> attachments{};
        // 0 for ray entry
        attachments[0].format = VolumeRendererExtVulkanSharedResourceWrapper::rayEntryImageFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 1 for ray exit
        attachments[1].format = VolumeRendererExtVulkanSharedResourceWrapper::rayExitImageFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 2 for ray cast(color)
        //debug
#ifdef DEBUG_WINDOW
        attachments[2].format =VK_FORMAT_B8G8R8A8_UNORM;// VolumeRendererVulkanSharedResourceWrapper::colorImageFormat;
#else
        attachments[2].format = VolumeRendererExtVulkanSharedResourceWrapper::colorImageFormat;
#endif
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // 3 for depth
        attachments[3].format = VolumeRendererExtVulkanSharedResourceWrapper::depthImageFormat;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        //first render pass
        {
            VkSubpassDescription subpassDesc{};

            VkAttachmentReference colorRefs[2];
            colorRefs[0] = {0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            colorRefs[1] = {1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

            VkAttachmentReference depthRef = {3,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

            subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDesc.colorAttachmentCount = 2;
            subpassDesc.pColorAttachments = colorRefs;
            subpassDesc.pDepthStencilAttachment = &depthRef;

            std::array<VkSubpassDependency,2> dependencies{};
            dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass = 0;
            dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT| VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcSubpass = 0;
            dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpassDesc;
            renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
            renderPassInfo.pDependencies = dependencies.data();

            VK_EXPR(vkCreateRenderPass(device,&renderPassInfo,nullptr,&renderer_vk_res->firstRenderPass));
        }
        //second render pass
        {
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

            attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[3].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
#ifdef DEBUG_WINDOW
            attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;//VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;//todo VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#else
            attachments[2].finalLayout =   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
#endif


            VkSubpassDescription subpassDesc{};

            VkAttachmentReference colorRefs[3];
            colorRefs[0] = {0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            colorRefs[1] = {1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            colorRefs[2] = {2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

            VkAttachmentReference depthRef = {3,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

            subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDesc.colorAttachmentCount = 0;
            subpassDesc.pColorAttachments = nullptr;
            subpassDesc.pDepthStencilAttachment = &depthRef;

            std::array<VkSubpassDependency,2> dependencies{};
            dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass = 0;
            dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT| VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            dependencies[1].srcSubpass = 0;
            dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            renderPassInfo.pAttachments = attachments.data();
            renderPassInfo.subpassCount = 1;
            renderPassInfo.pSubpasses = &subpassDesc;
            renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
            renderPassInfo.pDependencies = dependencies.data();

            VK_EXPR(vkCreateRenderPass(device,&renderPassInfo,nullptr,&renderer_vk_res->secondRenderPass));
        }
    }
    //create descriptor set layout
    {
        //ray pos
        {
            VkDescriptorSetLayoutBinding mvpLayoutBingding{};
            mvpLayoutBingding.binding = 0;
            mvpLayoutBingding.descriptorCount = 1;
            mvpLayoutBingding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            mvpLayoutBingding.pImmutableSamplers = nullptr;
            mvpLayoutBingding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            VkDescriptorSetLayoutBinding viewPosBinding{};
            viewPosBinding.binding = 1;
            viewPosBinding.descriptorCount = 1;
            viewPosBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            viewPosBinding.pImmutableSamplers = nullptr;
            viewPosBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding bindings[2] = {mvpLayoutBingding, viewPosBinding};
            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 2;
            layoutInfo.pBindings = bindings;

            VK_EXPR(vkCreateDescriptorSetLayout(renderer_vk_res->shared_device,&layoutInfo,nullptr,&renderer_vk_res->rayPosLayout));
        }
        //ray cast
        {
            VkDescriptorSetLayoutBinding rayEntryBinding{};
            rayEntryBinding.binding = 0;
            rayEntryBinding.descriptorCount = 1;
            rayEntryBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            rayEntryBinding.pImmutableSamplers = nullptr;
            rayEntryBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding rayExitBinding{};
            rayExitBinding.binding = 1;
            rayExitBinding.descriptorCount = 1;
            rayExitBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            rayExitBinding.pImmutableSamplers = nullptr;
            rayExitBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding interColorBinding{};
            rayEntryBinding.binding = 2;
            rayEntryBinding.descriptorCount = 1;
            rayEntryBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            rayEntryBinding.pImmutableSamplers = nullptr;
            rayEntryBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding renderPassTagBinding{};
            rayEntryBinding.binding = 3;
            rayEntryBinding.descriptorCount = 1;
            rayEntryBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            rayEntryBinding.pImmutableSamplers = nullptr;
            rayEntryBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding tfBinding{};
            tfBinding.binding = 4;
            tfBinding.descriptorCount = 1;
            tfBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            tfBinding.pImmutableSamplers = nullptr;
            tfBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding cachedVolumeBinding{};
            cachedVolumeBinding.binding = 5;
            cachedVolumeBinding.descriptorCount = GPUResource::DefaultMaxGPUTextureCount;
            cachedVolumeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            cachedVolumeBinding.pImmutableSamplers = nullptr;
            cachedVolumeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding volumeInfoBinding{};
            volumeInfoBinding.binding = 6;
            volumeInfoBinding.descriptorCount = 1;
            volumeInfoBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            volumeInfoBinding.pImmutableSamplers = nullptr;
            volumeInfoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding pageTableBinding{};
            pageTableBinding.binding = 7;
            pageTableBinding.descriptorCount = 1;
            pageTableBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            pageTableBinding.pImmutableSamplers = nullptr;
            pageTableBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding renderParamsBinding{};
            renderParamsBinding.binding = 8;
            renderParamsBinding.descriptorCount = 1;
            renderParamsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            renderParamsBinding.pImmutableSamplers = nullptr;
            renderParamsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutBinding bindings[9] = {rayEntryBinding,     rayExitBinding,
                                                        interColorBinding, renderPassTagBinding,
                                                        tfBinding,
                                                        cachedVolumeBinding, volumeInfoBinding, pageTableBinding,
                                                        renderParamsBinding};
            VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
            layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutCreateInfo.bindingCount = 9;
            layoutCreateInfo.pBindings = bindings;
            VK_EXPR(vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr, &renderer_vk_res->rayCastLayout));
        }
    }
    //create pipeline
    {
        //first ray pos
        {
            auto vertShaderCode = readShaderFile(ShaderAssetPath + "volume_render_pos.vert.spv");
            auto fragShaderCode = readShaderFile(ShaderAssetPath + "volume_render_pos.frag.spv");
            VkShaderModule vertShaderModule = createShaderModule(device,vertShaderCode);
            VkShaderModule fragShaderModule = createShaderModule(device,fragShaderCode);
            VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
            vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertShaderModule;
            vertShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
            fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo shaderStages[]={vertShaderStageInfo,fragShaderStageInfo};

            auto bindingDescription = getVertexBindingDescription();
            auto attributeDescription = getVertexAttributeDescription();

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = 1;
            vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            VkViewport  viewport{};
            viewport.x = 0.f;
            viewport.y = 0.f;
            viewport.width = (float)width;
            viewport.height = (float)height;
            viewport.minDepth= 0.f;
            viewport.maxDepth = 1.f;

            VkRect2D scissor{};
            scissor.offset = {0,0};
            scissor.extent = {(uint32_t)width,(uint32_t)height};

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;//useful for shadow map
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;//not use cull mode
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_FALSE;
            depthStencil.depthWriteEnable = VK_FALSE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
            depthStencil.depthBoundsTestEnable = VK_FALSE;
            depthStencil.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                                  | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;//!!!
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {colorBlendAttachment,colorBlendAttachment};

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY;
            colorBlending.attachmentCount = 2;
            colorBlending.pAttachments = colorBlendAttachments;
            colorBlending.blendConstants[0] = 0.f;
            colorBlending.blendConstants[1] = 0.f;
            colorBlending.blendConstants[2] = 0.f;
            colorBlending.blendConstants[3] = 0.f;


            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &renderer_vk_res->rayPosLayout;


            VK_EXPR(vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&renderer_vk_res->pipelineLayout.rayPos));

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.layout = renderer_vk_res->pipelineLayout.rayPos;
            pipelineInfo.renderPass = renderer_vk_res->firstRenderPass;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

            VK_EXPR(vkCreateGraphicsPipelines(device,nullptr,1,&pipelineInfo,nullptr,&renderer_vk_res->pipeline.rayPos));
            vkDestroyShaderModule(device,fragShaderModule,nullptr);
            vkDestroyShaderModule(device,vertShaderModule,nullptr);
        }
        //second ray cast
        {
            auto vertShaderCode = readShaderFile(ShaderAssetPath + "volume_renderPass_shading.vert.spv");
            auto fragShaderCode = readShaderFile(ShaderAssetPath + "volume_renderPass_shading.frag.spv");
            VkShaderModule vertShaderModule = createShaderModule(device,vertShaderCode);
            VkShaderModule fragShaderModule = createShaderModule(device,fragShaderCode);

            VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
            vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertShaderModule;
            vertShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
            fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo shaderStages[]={vertShaderStageInfo,fragShaderStageInfo};

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 0;//hard code for quad
            vertexInputInfo.pVertexBindingDescriptions = nullptr;
            vertexInputInfo.vertexAttributeDescriptionCount = 0;
            vertexInputInfo.pVertexAttributeDescriptions = nullptr;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            VkViewport  viewport{};
            viewport.x = 0.f;
            viewport.y = 0.f;
            viewport.width = (float)width;
            viewport.height = (float)height;
            viewport.minDepth= 0.f;
            viewport.maxDepth = 1.f;

            VkRect2D scissor{};
            scissor.offset = {0,0};
            scissor.extent = {(uint32_t)width,(uint32)height};

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;//useful for shadow map
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencil.depthBoundsTestEnable = VK_FALSE;
            depthStencil.stencilTestEnable = VK_FALSE;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                                  | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;
            VkPipelineColorBlendAttachmentState colorBlendAttachments[] = {colorBlendAttachment};

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = colorBlendAttachments;
            colorBlending.blendConstants[0] = 0.f;
            colorBlending.blendConstants[1] = 0.f;
            colorBlending.blendConstants[2] = 0.f;
            colorBlending.blendConstants[3] = 0.f;

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &renderer_vk_res->rayCastLayout;

            VK_EXPR(vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&renderer_vk_res->pipelineLayout.rayCast));

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.layout = renderer_vk_res->pipelineLayout.rayCast;
            pipelineInfo.renderPass = renderer_vk_res->secondRenderPass;
            pipelineInfo.subpass = 1;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

            VK_EXPR(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&renderer_vk_res->pipeline.rayCast));
            vkDestroyShaderModule(device,fragShaderModule,nullptr);
            vkDestroyShaderModule(device,vertShaderModule,nullptr);
        }
    }
    //create descriptor pool
    {
        std::array<VkDescriptorPoolSize,3> poolSize{};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize[0].descriptorCount = max_renderer_num * (5+1);
        poolSize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSize[1].descriptorCount = max_renderer_num * (3+1);
        poolSize[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize[2].descriptorCount = max_renderer_num * (2+1);
        poolSize[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize[3].descriptorCount = max_renderer_num * (1 + 1);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSize.size();
        poolInfo.pPoolSizes = poolSize.data();
        poolInfo.maxSets = (max_renderer_num * 2);

        VK_EXPR(vkCreateDescriptorPool(device,&poolInfo,nullptr,&renderer_vk_res->descriptorPool));
    }
}

VulkanVolumeRendererExt* VulkanVolumeRendererExt::Create(VulkanNodeSharedResourceWrapper* node_vk_res){
    static std::unordered_map<VulkanNodeSharedResourceWrapper*,std::pair<VolumeRendererExtVulkanSharedResourceWrapper,bool>> renderer_vs_res_map;
    if(!renderer_vs_res_map[node_vk_res].second){
        SetupVulkanRendererSharedResources(node_vk_res,&renderer_vs_res_map[node_vk_res].first);
        CreateVulkanVolumeRendererExtSharedResources(&renderer_vs_res_map[node_vk_res].first,node_vk_res);
        renderer_vs_res_map[node_vk_res].second = true;
    }
    assert(node_vk_res);
    auto ret = new VulkanVolumeRendererExt();
    try{
        ret->impl = std::make_unique<Impl>();
        ret->impl->initResources(&renderer_vs_res_map[node_vk_res].first,node_vk_res);
    }
    catch (const std::exception& err)
    {
        LOG_ERROR("create vulkan volume renderer ext implement failed: {}",err.what());
        return nullptr;
    }
    return ret;
}

void VulkanVolumeRendererExt::setVolume(const Volume &volume)
{
    impl->setVolume(volume);
}
Renderer::Type VulkanVolumeRendererExt::getRendererType() const
{
    return Renderer::VOLUME_EXT;
}
const Framebuffer &VulkanVolumeRendererExt::getFrameBuffers() const
{
    return impl->getFrameRenderResult();
}
void VulkanVolumeRendererExt::updatePageTable(const std::vector<PageTableItem> &items)
{
    impl->updatePageTable(items);
}
void VulkanVolumeRendererExt::setTransferFunction(const TransferFunction &tf)
{
    TransferFunctionExt1D tf_ext1d{tf};
    ::mrayns::ComputeTransferFunction1DExt(tf_ext1d);
    impl->setTransferFunction1D(tf_ext1d.tf,tf_ext1d.TFDim,sizeof(tf_ext1d.tf)/sizeof(float));
}
void VulkanVolumeRendererExt::setTransferFunction(const TransferFunctionExt1D &tf)
{
    impl->setTransferFunction1D(tf.tf,tf.TFDim,sizeof(tf.tf)/sizeof(float));
}
void VulkanVolumeRendererExt::render(const VolumeRendererCamera &camera)
{
    bool e = this->renderPass(camera,true);
    if(!e)
        LOG_INFO("notice: call render for VolumeRendererExt but render is not finished");
}
bool VulkanVolumeRendererExt::renderPass(const VolumeRendererCamera &camera, bool newFrame)
{
    return impl->renderPass(camera,newFrame);
}
void VulkanVolumeRendererExt::destroy()
{
    impl->destroy();
}
VulkanVolumeRendererExt::~VulkanVolumeRendererExt()
{
}

}

MRAYNS_END
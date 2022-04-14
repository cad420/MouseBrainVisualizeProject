//
// Created by wyz on 2022/3/3.
//
#include "VulkanSliceRenderer.hpp"
#include "../../algorithm/ColorMapping.hpp"
#include "../../common/Logger.hpp"
#include "../GPUResource.hpp"
#include "../../Config.hpp"
#include "Common.hpp"
#include "../../Utils/Timer.hpp"
#include "../../algorithm/SliceHelper.hpp"
MRAYNS_BEGIN

namespace internal{





struct SliceRendererVulkanSharedResourceWrapper:public VulkanRendererResourceWrapper
{

    VkDescriptorSetLayout descriptorSetLayout;

    VkRenderPass renderPass;

    VkPipelineLayout pipelineLayout;

    VkPipeline pipeline;

    static constexpr VkFormat colorImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat depthImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
};

struct SliceRendererVulkanPrivateResourceWrapper{
    VkDescriptorSet descriptorSet;

    VkFramebuffer framebuffer;

    FramebufferAttachment depthAttachment;

    FramebufferAttachment colorAttachment;

    VkCommandBuffer drawCommand;
    VkCommandBuffer resultCopyCommand;

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
    UBO volumeInfoUBO;
    UBO renderInfoUBO;
    UBO pageTableUBO;

    VmaAllocator allocator;//for mapping table buffer

};

struct VulkanSliceRenderer::Impl{
    std::unique_ptr<SliceRendererVulkanPrivateResourceWrapper> renderer_vk_res;
    SliceRendererVulkanSharedResourceWrapper* shared_renderer_vk_res;
    VulkanNodeSharedResourceWrapper* node_vk_res;
    Framebuffer render_result;
    Volume volume;
    static constexpr int MaxVolumeLod = 12;
    struct VolumeInfo{
        Vector4f volume_board;//x y z max_lod
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
        Vector3f origin; uint32_t padding0 = 1;
        Vector3f normal; uint32_t padding1 = 2;
        Vector3f x_dir; uint32_t padding2 = 3;
        Vector3f y_dir; uint32_t padding3 = 4;
        Vector2f min_p;Vector2f max_p;
        Vector2i window;float voxels_per_pixel;int lod;
        float step; float depth; uint32_t render_type; uint32_t padding4 = 5;
    }render_info;

    const Framebuffer& getFrameRenderResult(){
//        START_TIMER
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &renderer_vk_res->resultCopyCommand;
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
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

        {
//            START_TIMER
            ::memcpy(render_result.getColors().data(), result_color_mapped_ptr, render_result.getColors().size());
//            STOP_TIMER("render result copy")
        }

//        STOP_TIMER("get render result")
        return render_result;
    }

    void render(const SliceExt& slice, RenderType type){
        //todo viewport
//        START_TIMER
        updateRenderParamsUBO(slice,type);

        //submit draw commands to queue
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &renderer_vk_res->drawCommand;
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
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
//        STOP_TIMER("vulkan render")

    }
    void setVolume(const Volume& volume){
        if(!volume.isValid()){
            LOG_ERROR("invalid volume");
            return;
        }
        this->volume = volume;

        updateVolumeInfo();

    }
    void destroy(){

    }
    void setTransferFunctionExt1D(const TransferFunctionExt1D& tf){
        const float* data = tf.tf;
        int dim = tf.TFDim;
        int length = sizeof(tf.tf) / sizeof(tf.tf[0]);
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
    void updateVolumeInfo(){
        assert(volume.isValid());
        Vector3ui volume_dim;
        volume.getVolumeDim(reinterpret_cast<int &>(volume_dim.x),
                            reinterpret_cast<int &>(volume_dim.y),
                            reinterpret_cast<int &>(volume_dim.z));
        volume.getVolumeSpace(volume_info.volume_space.x, volume_info.volume_space.y, volume_info.volume_space.z);
        volume_info.volume_board = Vector4f{volume_dim.x * volume_info.volume_space.x,
                                            volume_dim.y * volume_info.volume_space.y,
                                            volume_dim.z * volume_info.volume_space.z,1.f};
        volume_info.volume_board.w = volume.getMaxLod();
        volume_info.padding_block_length = volume.getBlockLength();
        volume_info.padding = volume.getBlockPadding();

        volume_info.virtual_block_length = volume_info.padding_block_length - volume_info.padding * 2;

        volume_info.lod0_block_dim = (volume_dim + volume_info.virtual_block_length - (uint32_t)1) / volume_info.virtual_block_length;

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
    void updateRenderParamsUBO(const SliceExt& slice,RenderType type){
        render_info.render_type = static_cast<uint32_t>(type);
        //check slice valid
        render_info.origin = slice.origin;
        render_info.normal = normalize(slice.normal);
        render_info.x_dir = normalize(slice.x_dir);
        render_info.y_dir = normalize(slice.y_dir);
        render_info.min_p = Vector2f(slice.region.min_x,slice.region.min_y);
        render_info.max_p = Vector2f(slice.region.max_x,slice.region.max_y);
        render_info.window = Vector2i(slice.n_pixels_w,slice.n_pixels_h);
        render_info.voxels_per_pixel = slice.voxels_per_pixel;
        render_info.lod = slice.lod;
        render_info.step = slice.step;
        render_info.depth = slice.depth;

        uploadRenderParamsUBO();
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
    void updatePageTable(const std::vector<PageTableItem>& items){
        page_table.clear();
        for(const auto& item:items){
            page_table.append({item.second,item.first});
        }

        uploadPageTable();
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
    void initResources(SliceRendererVulkanSharedResourceWrapper* render_vk_shared_res,
                       VulkanNodeSharedResourceWrapper* node_vk_res){
        this->shared_renderer_vk_res = render_vk_shared_res;
        this->node_vk_res = node_vk_res;

        auto physical_device = node_vk_res->physicalDevice;
        assert(physical_device);
        auto device = render_vk_shared_res->shared_device;
        auto width = SliceRenderer::MaxSliceW;
        auto height = SliceRenderer::MaxSliceH;
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
            renderer_vk_res->colorAttachment.format = SliceRendererVulkanSharedResourceWrapper::colorImageFormat;
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->colorAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
            std::array<VkImageView,2> attachments = {
#ifdef DEBUG_WINDOW
                debug.swapChainImageViews[0],//debug
#else
                renderer_vk_res->colorAttachment.view,
#endif
                renderer_vk_res->depthAttachment.view
            };
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = render_vk_shared_res->renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = width;
            framebufferInfo.height = height;
            framebufferInfo.layers = 1;
            VK_EXPR(vkCreateFramebuffer(device,&framebufferInfo,nullptr,&renderer_vk_res->framebuffer));
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
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

            bufferInfo.size = sizeof(VolumeInfo);
#ifdef DEBUG_WINDOW
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->volumeInfoUBO.buffer,&renderer_vk_res->volumeInfoUBO.allocation,nullptr));
#endif

            bufferInfo.size = sizeof(HashTable);
#ifdef DEBUG_WINDOW
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->pageTableUBO.buffer,&renderer_vk_res->pageTableUBO.allocation,nullptr));
#endif
#ifdef DEBUG_WINDOW
            bufferInfo.size = sizeof(RenderInfo);
#else
            VK_EXPR(vmaCreateBuffer(renderer_vk_res->allocator,&bufferInfo,&allocInfo,&renderer_vk_res->renderInfoUBO.buffer,&renderer_vk_res->renderInfoUBO.allocation,nullptr));
#endif
        }
        //create descriptor sets
        {
            VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocateInfo.descriptorPool = render_vk_shared_res->descriptorPool;
            allocateInfo.descriptorSetCount = 1;
            allocateInfo.pSetLayouts = &render_vk_shared_res->descriptorSetLayout;
            VK_EXPR(vkAllocateDescriptorSets(render_vk_shared_res->shared_device,&allocateInfo,&renderer_vk_res->descriptorSet));

            auto count = node_vk_res->textures.size();
            std::vector<VkDescriptorImageInfo> volumeImageInfo(count);
            for(int i = 0;i<volumeImageInfo.size();i++){
                volumeImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                volumeImageInfo[i].imageView = node_vk_res->textures[i].view;
                volumeImageInfo[i].sampler = node_vk_res->texture_sampler;
            }

            VkDescriptorImageInfo tfImageInfo{};
            tfImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            tfImageInfo.imageView = renderer_vk_res->tf.view;
            tfImageInfo.sampler = renderer_vk_res->tf.sampler;

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

            std::array<VkWriteDescriptorSet,5> descriptorWrites{};

            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = renderer_vk_res->descriptorSet;
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[0].descriptorCount = volumeImageInfo.size();
            descriptorWrites[0].pImageInfo = volumeImageInfo.data();

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = renderer_vk_res->descriptorSet;
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &tfImageInfo;

            descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = renderer_vk_res->descriptorSet;
            descriptorWrites[2].dstBinding = 2;
            descriptorWrites[2].dstArrayElement = 0;
            descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].pBufferInfo = &volumeBufferInfo;

            descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[3].dstSet = renderer_vk_res->descriptorSet;
            descriptorWrites[3].dstBinding = 3;
            descriptorWrites[3].dstArrayElement = 0;
            descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[3].descriptorCount = 1;
            descriptorWrites[3].pBufferInfo = &pageTableBufferInfo;

            descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[4].dstSet = renderer_vk_res->descriptorSet;
            descriptorWrites[4].dstBinding = 4;
            descriptorWrites[4].dstArrayElement = 0;
            descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[4].descriptorCount = 1;
            descriptorWrites[4].pBufferInfo = &renderBufferInfo;

            vkUpdateDescriptorSets(device,descriptorWrites.size(),descriptorWrites.data(),0,nullptr);

        }

        //create draw command buffer and record
        {
            std::lock_guard<std::mutex> lk(shared_renderer_vk_res->pool_mtx);
            VkCommandBufferAllocateInfo allocateInfo{};
            allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocateInfo.commandPool = shared_renderer_vk_res->shared_graphics_command_pool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1;
            VK_EXPR(vkAllocateCommandBuffers(shared_renderer_vk_res->shared_device,
                                             &allocateInfo,&renderer_vk_res->drawCommand));
            VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_EXPR(vkBeginCommandBuffer(renderer_vk_res->drawCommand,&beginInfo));
            VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            renderPassInfo.renderPass = shared_renderer_vk_res->renderPass;
            renderPassInfo.framebuffer = renderer_vk_res->framebuffer;
            renderPassInfo.renderArea = {0,0};
            renderPassInfo.renderArea.extent = {(uint32_t)width,(uint32_t)height};

            std::array<VkClearValue,2> clearValues{};
            clearValues[0].color = {0.f,0.f,0.f,0.f};
            clearValues[1].depthStencil = {1.f,0};
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            auto cmd = renderer_vk_res->drawCommand;

            vkCmdBeginRenderPass(cmd,&renderPassInfo,VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shared_renderer_vk_res->pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,shared_renderer_vk_res->pipelineLayout,
                                    0,1,&renderer_vk_res->descriptorSet,0,nullptr);
            vkCmdDraw(cmd,6,1,0,0);

            vkCmdEndRenderPass(cmd);
            VK_EXPR(vkEndCommandBuffer(cmd));
        }
        //create result color
        {
            render_result = Framebuffer(width,height);
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
            VmaAllocationInfo info{};
#ifdef DEBUG_WINDOW
            createBuffer(node_vk_res->physicalDevice,node_vk_res->device,bufferCreateInfo.size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         renderer_vk_res->result_color.buffer,renderer_vk_res->result_color.mem);
#else
            vmaCreateBuffer(renderer_vk_res->allocator,&bufferCreateInfo,&allocInfo,&renderer_vk_res->result_color.buffer,&renderer_vk_res->result_color.allocation,&info);
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
//            vmaMapMemory(renderer_vk_res->allocator,renderer_vk_res->result_color.allocation,&result_color_mapped_ptr);
            result_color_mapped_ptr = info.pMappedData;
#endif
        }
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
    Impl(){
        renderer_vk_res = std::make_unique<SliceRendererVulkanPrivateResourceWrapper>();
    }
    ~Impl(){
        destroy();
    }
};


static void CreateVulkanSliceRendererSharedResources(
    SliceRendererVulkanSharedResourceWrapper* renderer_vk_res,
    VulkanNodeSharedResourceWrapper* node_vk_res
    ){
    assert(node_vk_res && renderer_vk_res);
    auto physical_device = node_vk_res->physicalDevice;
    assert(physical_device);
    auto device = renderer_vk_res->shared_device;
    auto width = SliceRenderer::MaxSliceW;
    auto height = SliceRenderer::MaxSliceH;
    int max_renderer_num = GPUResource::DefaultMaxRendererCount;

    //create renderpass
    {
        //just need color and depth attchment
        std::array<VkAttachmentDescription,2> attachments{};
        // 0 for color
        attachments[0].format = SliceRendererVulkanSharedResourceWrapper::colorImageFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#ifdef DEBUG_WINDOW
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
#else
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
#endif
        attachments[1].format = SliceRendererVulkanSharedResourceWrapper::depthImageFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // only 1 subpass
        std::array<VkSubpassDescription,1> subpassDesc{};

        std::array<VkAttachmentReference,1> colorRefs{};
        colorRefs[0] = {0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkAttachmentReference depthRef = {1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        subpassDesc[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc[0].colorAttachmentCount = colorRefs.size();
        subpassDesc[0].pColorAttachments = colorRefs.data();
        subpassDesc[0].pDepthStencilAttachment = &depthRef;
        subpassDesc[0].inputAttachmentCount = 0;
        subpassDesc[0].pInputAttachments = nullptr;

        std::array<VkSubpassDependency,2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = subpassDesc.size();
        renderPassInfo.pSubpasses = subpassDesc.data();
        renderPassInfo.dependencyCount = dependencies.size();
        renderPassInfo.pDependencies = dependencies.data();

        VK_EXPR(vkCreateRenderPass(device,&renderPassInfo,nullptr,&renderer_vk_res->renderPass));
    }
    //descriptorset layout
    {
        VkDescriptorSetLayoutBinding cachedVolumeBinding{};
        cachedVolumeBinding.binding = 0;
        cachedVolumeBinding.descriptorCount = GPUResource::DefaultMaxGPUTextureCount;
        cachedVolumeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cachedVolumeBinding.pImmutableSamplers = nullptr;
        cachedVolumeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding tfBinding{};
        tfBinding.binding = 1;
        tfBinding.descriptorCount = 1;
        tfBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tfBinding.pImmutableSamplers = nullptr;
        tfBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding volumeInfoBinding{};
        volumeInfoBinding.binding = 2;
        volumeInfoBinding.descriptorCount = 1;
        volumeInfoBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        volumeInfoBinding.pImmutableSamplers = nullptr;
        volumeInfoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding pageTableBinding{};
        pageTableBinding.binding = 3;
        pageTableBinding.descriptorCount = 1;
        pageTableBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pageTableBinding.pImmutableSamplers = nullptr;
        pageTableBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding renderParamsBinding{};
        renderParamsBinding.binding = 4;
        renderParamsBinding.descriptorCount = 1;
        renderParamsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        renderParamsBinding.pImmutableSamplers = nullptr;
        renderParamsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding bindings[5] = {cachedVolumeBinding,     tfBinding,
                                                    volumeInfoBinding, pageTableBinding,
                                                    renderParamsBinding};
        VkDescriptorSetLayoutCreateInfo layoutCreateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutCreateInfo.bindingCount = 5;
        layoutCreateInfo.pBindings = bindings;
        VK_EXPR(vkCreateDescriptorSetLayout(device,&layoutCreateInfo,nullptr,&renderer_vk_res->descriptorSetLayout));
    }
    //create pipeline
    {
        auto vertShaderCode = readShaderFile(ShaderAssetPath + "slice_render.vert.spv");
        auto fragShaderCode = readShaderFile(ShaderAssetPath + "slice_render.frag.spv");
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
        pipelineLayoutInfo.pSetLayouts = &renderer_vk_res->descriptorSetLayout;

        VK_EXPR(vkCreatePipelineLayout(device,&pipelineLayoutInfo,nullptr,&renderer_vk_res->pipelineLayout));

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
        pipelineInfo.layout = renderer_vk_res->pipelineLayout;
        pipelineInfo.renderPass = renderer_vk_res->renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        VK_EXPR(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&renderer_vk_res->pipeline));
        vkDestroyShaderModule(device,fragShaderModule,nullptr);
        vkDestroyShaderModule(device,vertShaderModule,nullptr);
    }
    //create descriptor pool
    {
        std::array<VkDescriptorPoolSize,2> poolSize{};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize[0].descriptorCount = max_renderer_num * (5+1);
        poolSize[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize[1].descriptorCount = max_renderer_num * (2+1);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSize.size();
        poolInfo.pPoolSizes = poolSize.data();
        poolInfo.maxSets = (max_renderer_num * 2);

        VK_EXPR(vkCreateDescriptorPool(device,&poolInfo,nullptr,&renderer_vk_res->descriptorPool));
    }

}

Renderer::Type VulkanSliceRenderer::getRendererType() const
{
    return Renderer::SLICE;
}
const Framebuffer &VulkanSliceRenderer::getFrameBuffers() const
{
    return impl->getFrameRenderResult();
}
void VulkanSliceRenderer::render(const Slice &slice)
{
    float step = impl->volume_info.voxel * SliceHelper::SliceStepVoxelRatio;
    impl->render({slice,SliceHelper::GetSliceLod(slice),step,0.f},MIP);
}

VulkanSliceRenderer *VulkanSliceRenderer::Create(VulkanNodeSharedResourceWrapper *node_vk_res)
{
    static std::unordered_map<VulkanNodeSharedResourceWrapper*,std::pair<SliceRendererVulkanSharedResourceWrapper,bool>> renderer_vk_res_map;
    if(!renderer_vk_res_map[node_vk_res].second){
        SetupVulkanRendererSharedResources(node_vk_res,&renderer_vk_res_map[node_vk_res].first);
        CreateVulkanSliceRendererSharedResources(&renderer_vk_res_map[node_vk_res].first,node_vk_res);
        renderer_vk_res_map[node_vk_res].second = true;
    }
    assert(node_vk_res);
    auto ret = new VulkanSliceRenderer();
    try{
        ret->impl = std::make_unique<Impl>();
        ret->impl->initResources(&renderer_vk_res_map[node_vk_res].first,node_vk_res);
    }
    catch (const std::exception& err)
    {
        LOG_ERROR("create vulkan slice renderer implement failed: {}",err.what());
        return nullptr;
    }
    return ret;
}
VulkanSliceRenderer::~VulkanSliceRenderer()
{
}
void VulkanSliceRenderer::destroy()
{
    impl->destroy();
}
void VulkanSliceRenderer::setVolume(Volume volume)
{
    impl->setVolume(volume);
}
void VulkanSliceRenderer::render(const SliceExt &slice, SliceRenderer::RenderType type)
{
    impl->render(slice,type);
}
void VulkanSliceRenderer::setTransferFunction(TransferFunction tf)
{
    TransferFunctionExt1D transferFunctionExt1D{tf};
    ::mrayns::ComputeTransferFunction1DExt(transferFunctionExt1D);
    impl->setTransferFunctionExt1D(transferFunctionExt1D);
}
void VulkanSliceRenderer::updatePageTable(const std::vector<PageTableItem> &items)
{
    impl->updatePageTable(items);
}

}


MRAYNS_END

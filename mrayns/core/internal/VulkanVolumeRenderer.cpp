//
// Created by wyz on 2022/3/3.
//
#include "VulkanVolumeRenderer.hpp"
#include "../../common/Logger.hpp"
#include "../../geometry/Mesh.hpp"
#include "../GPUResource.hpp"
#include <array>
#include "../../Config.hpp"
MRAYNS_BEGIN

namespace internal{



struct VolumeRendererVulkanSharedResourceWrapper:public VulkanRendererResourceWrapper
{
    VkPushConstantRange rayPosPushConstMVP{};
    VkPushConstantRange rayPosPushConstViewPos{};

    VkDescriptorSetLayout rayCastLayout{};


    VkRenderPass renderPass{};

    struct{
        VkPipelineLayout rayPos;
        VkPipelineLayout rayCast;
    }pipelineLayout{};
     struct{
         VkPipeline rayPos;
         VkPipeline rayCast;
     }pipeline{};

     static constexpr VkFormat rayEntryImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
     static constexpr VkFormat rayExitImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
     static constexpr VkFormat depthImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
     static constexpr VkFormat colorImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
};

struct VolumeRendererVulkanPrivateResourceWrapper{
    //private resource
    //only for the renderer instance not shared with the same type renderer
    VkDescriptorSet descriptorSet;

    //depth and color image mem alloc use default because they're not changed since created

    FramebufferAttachment rayEntryAttachment;
    FramebufferAttachment rayExitAttachment;

    FramebufferAttachment depthAttachment;

    FramebufferAttachment colorAttachment;

    struct{
       VkImage image;
       VkImageView view;
       VkDeviceMemory mem;
       VkSampler sampler;
    }tf;
    static constexpr int MaxVolumeLod = 12;
    struct VolumeInfo{
        Vector4i volume_dim;
        Vector3i lod0_block_dim;
        int block_length;
        int padding;
        Vector3f volume_space;
        Vector3f inv_volume_space;
        float voxel;
        uint32_t lod_page_table_offset[MaxVolumeLod];
        Vector3f inv_texture_shape;
    }volumeInfo;
    struct RenderInfo{
        float lod_dist[MaxVolumeLod];
        float ray_step;
        Vector3f view_pos;
    };
    struct UBO{
        VkBuffer buffer;
        VkDeviceMemory mem;
    };
    UBO volumeInfoUBO;
    UBO renderInfoUBO;

    struct{
        VkBuffer buffer;
        VmaAllocation allocation;
        size_t size;
    }pageTableSBO;

    VkFramebuffer framebuffer;

    VkCommandBuffer commandBuffer;
    //not thread-safe
    VmaAllocator allocator;//for mapping table buffer

    struct{
        VkBuffer vertexBuffer;
        VmaAllocation vertexAllocation;
        VkBuffer indexBuffer;
        VmaAllocation indexAllocation;
    }proxyCubeBuffer;
};
//renderer是被单线程调用的 所以不需要考虑加锁
struct VulkanVolumeRenderer::Impl{
    std::unique_ptr<VolumeRendererVulkanPrivateResourceWrapper> renderer_vk_res;

    Framebuffer render_result;


    void render(const VolumeRendererCamera& camera){
        //0. check renderer resource

        //1. resize Framebuffer


    }

    const Framebuffer& getFrameRenderResult() const{
        return render_result;
    }

    void destroy(){

    }

    void setVolume(Volume volume){
        //0. check if resource about this volume is cached

        //0.1 reload cached resource

        //0.2 create resource for the volume

    }
    void createRendererVKResPageTableBuffer(){

    }
    void initResources(
        VolumeRendererVulkanSharedResourceWrapper* render_vk_shared_res,
        VulkanNodeSharedResourceWrapper* node_vk_res){
        assert(renderer_vk_res.get() && render_vk_shared_res && node_vk_res);

        auto physical_device = node_vk_res->physicalDevice;
        assert(physical_device);
        auto device = render_vk_shared_res->shared_device;
        auto width = VolumeRendererVulkanSharedResourceWrapper::DefaultFrameWidth;
        auto height = VolumeRendererVulkanSharedResourceWrapper::DefaultFrameHeight;
        int max_renderer_num = GPUResource::DefaultMaxRendererCount;

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
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->colorAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->colorAttachment.image,renderer_vk_res->colorAttachment.mem);
            createImageView(device,renderer_vk_res->colorAttachment.image,renderer_vk_res->colorAttachment.format,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->colorAttachment.view);
        }
        //create ray pos attachment
        {
            renderer_vk_res->rayEntryAttachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->rayEntryAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->rayEntryAttachment.image,renderer_vk_res->rayEntryAttachment.mem);
            createImageView(device,renderer_vk_res->rayEntryAttachment.image,renderer_vk_res->rayEntryAttachment.format,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->rayEntryAttachment.view);
            renderer_vk_res->rayExitAttachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            createImage(physical_device,device,width,height,1,VK_SAMPLE_COUNT_1_BIT,renderer_vk_res->rayExitAttachment.format,
                        VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        renderer_vk_res->rayExitAttachment.image,renderer_vk_res->rayExitAttachment.mem);
            createImageView(device,renderer_vk_res->rayExitAttachment.image,renderer_vk_res->rayExitAttachment.format,
                            VK_IMAGE_ASPECT_COLOR_BIT,1,renderer_vk_res->rayExitAttachment.view);
        }
        //create framebuffer
        {
            std::array<VkImageView,4> attachments = {
                renderer_vk_res->rayEntryAttachment.view,
                renderer_vk_res->rayExitAttachment.view,
                renderer_vk_res->colorAttachment.view,
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

        }
        //create ubo
        {

        }
        //create descriptor sets
        {
            //none for ray pos

            //ray cast
            {
                VkDescriptorImageInfo rayEntryImageInfo{};
                rayEntryImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                rayEntryImageInfo.imageView = renderer_vk_res->rayEntryAttachment.view;
                rayEntryImageInfo.sampler = VK_NULL_HANDLE;

                VkDescriptorImageInfo rayExitImageInfo{};
                rayExitImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                rayExitImageInfo.imageView = renderer_vk_res->rayExitAttachment.view;
                rayExitImageInfo.sampler = VK_NULL_HANDLE;

                VkDescriptorImageInfo tfImageInfo{};
                tfImageInfo.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR;
                tfImageInfo.imageView = renderer_vk_res->tf.view;
                tfImageInfo.sampler = renderer_vk_res->tf.sampler;

                std::vector<VkDescriptorImageInfo> volumeImageInfo(node_vk_res->textures.size());
                for(int i = 0;i<volumeImageInfo.size();i++){
                    volumeImageInfo[i].imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR;
                    volumeImageInfo[i].imageView = node_vk_res->textures[i].view;
                    volumeImageInfo[i].sampler = node_vk_res->texture_sampler;
                }

                VkDescriptorBufferInfo volumeBufferInfo;
                volumeBufferInfo.buffer = renderer_vk_res->volumeInfoUBO.buffer;
                volumeBufferInfo.offset = 0;
                volumeBufferInfo.range = sizeof(VolumeRendererVulkanPrivateResourceWrapper::VolumeInfo);

                VkDescriptorBufferInfo pageTableBufferInfo{};
                pageTableBufferInfo.buffer = renderer_vk_res->pageTableSBO.buffer;
                pageTableBufferInfo.offset = 0;
                pageTableBufferInfo.range = renderer_vk_res->pageTableSBO.size;

                VkDescriptorBufferInfo renderBufferInfo{};
                renderBufferInfo.buffer = renderer_vk_res->renderInfoUBO.buffer;
                renderBufferInfo.offset = 0;
                renderBufferInfo.range = sizeof(VolumeRendererVulkanPrivateResourceWrapper::RenderInfo);

                std::array<VkWriteDescriptorSet,7> descriptorWrites{};
                descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[0].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[0].dstBinding = 0;
                descriptorWrites[0].dstArrayElement = 0;
                descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                descriptorWrites[0].descriptorCount = 1;
                descriptorWrites[0].pImageInfo = &rayEntryImageInfo;

                descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[1].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[1].dstBinding = 1;
                descriptorWrites[1].dstArrayElement = 0;
                descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                descriptorWrites[1].descriptorCount = 1;
                descriptorWrites[1].pImageInfo = &rayExitImageInfo;

                descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[2].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[2].dstBinding = 2;
                descriptorWrites[2].dstArrayElement = 0;
                descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[2].descriptorCount = 1;
                descriptorWrites[2].pImageInfo = &tfImageInfo;

                descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[3].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[3].dstBinding = 3;
                descriptorWrites[3].dstArrayElement = 0;
                descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrites[3].descriptorCount = volumeImageInfo.size();
                descriptorWrites[3].pImageInfo = volumeImageInfo.data();

                descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[4].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[4].dstBinding = 4;
                descriptorWrites[4].dstArrayElement = 0;
                descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[4].descriptorCount = 1;
                descriptorWrites[4].pBufferInfo = &volumeBufferInfo;

                descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[5].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[5].dstBinding = 5;
                descriptorWrites[5].dstArrayElement = 0;
                descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[5].descriptorCount = 1;
                descriptorWrites[5].pBufferInfo = &pageTableBufferInfo;

                descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[6].dstSet = renderer_vk_res->descriptorSet;
                descriptorWrites[6].dstBinding = 6;
                descriptorWrites[6].dstArrayElement = 0;
                descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                descriptorWrites[6].descriptorCount = 1;
                descriptorWrites[6].pBufferInfo = &renderBufferInfo;

                vkUpdateDescriptorSets(device,descriptorWrites.size(),descriptorWrites.data(),0,nullptr);
            }
        }
    }

    Impl(){
        renderer_vk_res = std::make_unique<VolumeRendererVulkanPrivateResourceWrapper>();
    }
    ~Impl(){
        destroy();
    }
};



static void CreateVulkanVolumeRendererSharedResources(
    VolumeRendererVulkanSharedResourceWrapper* renderer_vk_res,
    VulkanNodeSharedResourceWrapper* node_vk_res
    )
{
    assert(node_vk_res && renderer_vk_res);
    auto physical_device = node_vk_res->physicalDevice;
    assert(physical_device);
    auto device = renderer_vk_res->shared_device;
    auto width = VolumeRendererVulkanSharedResourceWrapper::DefaultFrameWidth;
    auto height = VolumeRendererVulkanSharedResourceWrapper::DefaultFrameHeight;
    int max_renderer_num = GPUResource::DefaultMaxRendererCount;

    //create renderpass
    {
        std::array<VkAttachmentDescription,4> attachments{};
        // 0 for ray entry
        attachments[0].format = VolumeRendererVulkanSharedResourceWrapper::rayEntryImageFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 1 for ray exit
        attachments[1].format = VolumeRendererVulkanSharedResourceWrapper::rayExitImageFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 2 for ray cast(color)
        attachments[2].format = VolumeRendererVulkanSharedResourceWrapper::colorImageFormat;
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // 3 for depth
        attachments[3].format = VolumeRendererVulkanSharedResourceWrapper::depthImageFormat;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // has 2 subpass
        std::array<VkSubpassDescription,2> subpassDesc{};

        // first subpass: get ray entry and ray exit attachment
        VkAttachmentReference colorRefs[2];
        colorRefs[0] = {0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[1] = {1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkAttachmentReference depthRef = {3,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        subpassDesc[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc[0].colorAttachmentCount = 2;
        subpassDesc[0].pColorAttachments = colorRefs;
        subpassDesc[0].pDepthStencilAttachment = &depthRef;
        subpassDesc[0].inputAttachmentCount = 0;
        subpassDesc[0].pInputAttachments = nullptr;

        // second subpass: ray cast render to color attachment
        VkAttachmentReference colorRef = {2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference inputRefs[2];
        inputRefs[0] = {0,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        inputRefs[1] = {1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        subpassDesc[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc[1].colorAttachmentCount = 1;
        subpassDesc[1].pColorAttachments = &colorRef;
        subpassDesc[1].pDepthStencilAttachment = &depthRef;
        subpassDesc[1].inputAttachmentCount = 2;
        subpassDesc[1].pInputAttachments = inputRefs;

        //use subpass dependencies for layout transitions
        std::array<VkSubpassDependency,3> dependencies{};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = 1;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[2].srcSubpass = 1;
        dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDesc.size());
        renderPassInfo.pSubpasses = subpassDesc.data();
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VK_EXPR(vkCreateRenderPass(device,&renderPassInfo,nullptr,&renderer_vk_res->renderPass));
    }

    //create push constant and descriptorset layout
    {
        //ray pos
        renderer_vk_res->rayPosPushConstMVP = VkPushConstantRange{
            VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(Matrix4f)*2
        };
        renderer_vk_res->rayPosPushConstViewPos = VkPushConstantRange{
            VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(Matrix4f)*2,sizeof(Vector4f)
        };

        //ray cast
        VkDescriptorSetLayoutBinding rayEntryBinding{};
        rayEntryBinding.binding = 0;
        rayEntryBinding.descriptorCount = 1;
        rayEntryBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        rayEntryBinding.pImmutableSamplers = nullptr;
        rayEntryBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding rayExitBinding{};
        rayExitBinding.binding = 1;
        rayExitBinding.descriptorCount = 1;
        rayExitBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        rayExitBinding.pImmutableSamplers = nullptr;
        rayExitBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding tfBinding{};
        tfBinding.binding = 2;
        tfBinding.descriptorCount = 1;
        tfBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tfBinding.pImmutableSamplers = nullptr;
        tfBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding cachedVolumeBinding{};
        cachedVolumeBinding.binding = 3;
        cachedVolumeBinding.descriptorCount = GPUResource::DefaultMaxGPUTextureCount;
        cachedVolumeBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cachedVolumeBinding.pImmutableSamplers = nullptr;
        cachedVolumeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding volumeInfoBinding{};
        volumeInfoBinding.binding = 4;
        volumeInfoBinding.descriptorCount = 1;
        volumeInfoBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        volumeInfoBinding.pImmutableSamplers = nullptr;
        volumeInfoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding pageTableBinding{};
        pageTableBinding.binding = 5;
        pageTableBinding.descriptorCount = 1;
        pageTableBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pageTableBinding.pImmutableSamplers = nullptr;
        pageTableBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding renderParamsBinding{};
        renderParamsBinding.binding = 6;
        renderParamsBinding.descriptorCount = 1;
        renderParamsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        renderParamsBinding.pImmutableSamplers = nullptr;
        renderParamsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding bindings[7]={
            rayEntryBinding,rayExitBinding,tfBinding,cachedVolumeBinding,
            volumeInfoBinding,pageTableBinding,renderParamsBinding
        };
        VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
        layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutCreateInfo.bindingCount = 7;
        layoutCreateInfo.pBindings = bindings;
        VK_EXPR(vkCreateDescriptorSetLayout(device,&layoutCreateInfo,nullptr,&renderer_vk_res->rayCastLayout));
    }
    //create pipeline
    {
        //ray pos
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
            pipelineLayoutInfo.setLayoutCount = 0;
            pipelineLayoutInfo.pSetLayouts = nullptr;

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
            pipelineInfo.renderPass = renderer_vk_res->renderPass;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

            VK_EXPR(vkCreateGraphicsPipelines(device,nullptr,1,&pipelineInfo,nullptr,&renderer_vk_res->pipeline.rayPos));
            vkDestroyShaderModule(device,fragShaderModule,nullptr);
            vkDestroyShaderModule(device,vertShaderModule,nullptr);
        }
        //ray cast
        {
            auto vertShaderCode = readShaderFile(ShaderAssetPath + "volume_render_shading.vert.spv");
            auto fragShaderCode = readShaderFile(ShaderAssetPath + "volume_render_shading.frag.spv");
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
            pipelineInfo.renderPass = renderer_vk_res->renderPass;
            pipelineInfo.subpass = 1;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

            VK_EXPR(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&renderer_vk_res->pipeline.rayCast));
            vkDestroyShaderModule(device,fragShaderModule,nullptr);
            vkDestroyShaderModule(device,vertShaderModule,nullptr);
        }
    }
    //create descriptor pool

    {
        std::array<VkDescriptorPoolSize,4> poolSize{};
        poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize[0].descriptorCount = max_renderer_num * (2+1);
        poolSize[1].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        poolSize[1].descriptorCount = max_renderer_num * (2+1);
        poolSize[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize[2].descriptorCount = max_renderer_num * (1 + 1);
        poolSize[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize[3].descriptorCount = max_renderer_num * (2+1);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSize.size();
        poolInfo.pPoolSizes = poolSize.data();
        poolInfo.maxSets = (max_renderer_num * 2);

        VK_EXPR(vkCreateDescriptorPool(device,&poolInfo,nullptr,&renderer_vk_res->descriptorPool));
    }
}

VulkanVolumeRenderer* VulkanVolumeRenderer::Create(VulkanNodeSharedResourceWrapper *node_vk_res)
{
    static VolumeRendererVulkanSharedResourceWrapper renderer_vk_res;
    static bool renderer_shared_vk_res_init = false;
    //ok for multi-threading because outside caller is locked
    if(!renderer_shared_vk_res_init){
        //will throw exception if shared resource init failed
        SetupVulkanRendererSharedResources(node_vk_res,&renderer_vk_res);

        CreateVulkanVolumeRendererSharedResources(&renderer_vk_res,node_vk_res);

        renderer_shared_vk_res_init = true;
    }
    assert(node_vk_res);
    auto ret = new VulkanVolumeRenderer();
    try{
        ret->impl = std::make_unique<Impl>();
        ret->impl->initResources(&renderer_vk_res,node_vk_res);
    }
    catch (const std::exception& err)
    {
        LOG_ERROR("create vulkan volume renderer implement failed");
        return nullptr;
    }
    return ret;
}

Renderer::Type VulkanVolumeRenderer::getRendererType() const
{
    return Renderer::VOLUME;
}
const Framebuffer &VulkanVolumeRenderer::getFrameBuffers() const
{
    return impl->getFrameRenderResult();
}
void VulkanVolumeRenderer::render(const VolumeRendererCamera &camera)
{
    impl->render(camera);
}
VulkanVolumeRenderer::~VulkanVolumeRenderer()
{

}
void VulkanVolumeRenderer::destroy()
{
    impl->destroy();
}
void VulkanVolumeRenderer::setVolume(Volume)
{
}
}



MRAYNS_END
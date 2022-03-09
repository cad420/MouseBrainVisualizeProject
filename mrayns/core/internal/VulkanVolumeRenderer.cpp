//
// Created by wyz on 2022/3/3.
//
#include "VulkanVolumeRenderer.hpp"
#include "../../common/Logger.hpp"
MRAYNS_BEGIN

namespace internal{



struct VolumeRendererVulkanResourceWrapper:public VulkanRendererSharedResourceWrapper{
    VkDescriptorSet descriptorSet;
    VkCommandBuffer commandBuffer;
    VmaAllocator allocator;//for mapping table buffer
};
//renderer是被单线程调用的 所以不需要考虑加锁
struct VulkanVolumeRenderer::Impl{
    std::unique_ptr<VolumeRendererVulkanResourceWrapper> renderer_vk_res;

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
    void initResources(VulkanNodeSharedResourceWrapper* node_vk_res){
        assert(renderer_vk_res.get());

    }

    Impl(){
        renderer_vk_res = std::make_unique<VolumeRendererVulkanResourceWrapper>();
    }
    ~Impl(){
        destroy();
    }
};
static void CreateVulkanRendererSharedResources(VulkanNodeSharedResourceWrapper* node_vk_res,
                                         VulkanRendererSharedResourceWrapper* renderer_vk_res){
    assert(node_vk_res && renderer_vk_res);
    renderer_vk_res->device = node_vk_res->device;
    vkGetDeviceQueue(node_vk_res->device,node_vk_res->graphicsQueueFamilyIndex,0,&renderer_vk_res->graphicsQueue);
    assert(renderer_vk_res->graphicsQueue);


}
VulkanVolumeRenderer* VulkanVolumeRenderer::Create(VulkanNodeSharedResourceWrapper *node_vk_res)
{
    static VulkanRendererSharedResourceWrapper renderer_shared_vk_res;
    static bool renderer_shared_vk_res_init = false;
    //ok for multi-threading because outside caller is locked
    if(!renderer_shared_vk_res_init){


        renderer_shared_vk_res_init = true;
    }
    assert(node_vk_res);
    auto ret = new VulkanVolumeRenderer();
    try{
        ret->impl = std::make_unique<Impl>();
        ret->impl->initResources(node_vk_res);
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
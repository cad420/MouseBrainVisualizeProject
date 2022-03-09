//
// Created by wyz on 2022/3/3.
//
#pragma once
#include "VulkanUtil.hpp"
#include "../Renderer.hpp"
MRAYNS_BEGIN
namespace internal{



class VulkanVolumeRenderer:public VolumeRenderer{
  public:

    void setVolume(Volume) override;
    Type getRendererType() const override;
    const Framebuffer& getFrameBuffers() const override;
    void render(const VolumeRendererCamera&) override;

    static VulkanVolumeRenderer* Create(VulkanNodeSharedResourceWrapper*);

    friend class VulkanVolumeRendererDeleter;
    friend class VulkanRendererDeleter;

  private:

    void destroy();
    ~VulkanVolumeRenderer() override;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class VulkanVolumeRendererDeleter{
  public:
    constexpr VulkanVolumeRendererDeleter() noexcept = default;
    VulkanVolumeRendererDeleter(const VulkanVolumeRendererDeleter&) noexcept = default;
    void operator()(VulkanVolumeRenderer* ptr) const noexcept{
        ptr->destroy();
    }
};
}


MRAYNS_END
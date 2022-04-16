//
// Created by wyz on 2022/4/15.
//
#pragma once
#include "VulkanUtil.hpp"
#include "../Renderer.hpp"

MRAYNS_BEGIN
namespace internal{

class VulkanVolumeRendererExt:public VolumeRendererExt{
  public:

    void setVolume(const Volume&) override;
    Type getRendererType() const override;
    const Framebuffer& getFrameBuffers() const override;
    void updatePageTable(const std::vector<PageTableItem>&) override;
    void setTransferFunction(const TransferFunction&) override;
    void setTransferFunction(const TransferFunctionExt1D&) override;
    void render(const VolumeRendererCamera&) override;
    bool renderPass(const VolumeRendererCamera&,bool) override;
    static VulkanVolumeRendererExt* Create(VulkanNodeSharedResourceWrapper*);

    friend class VulkanVolumeRendererExtDeleter;
    friend class VulkanRendererDeleter;

  private:

    void destroy();
    ~VulkanVolumeRendererExt() override;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class VulkanVolumeRendererExtDeleter{
  public:
    constexpr VulkanVolumeRendererExtDeleter() noexcept = default;
    VulkanVolumeRendererExtDeleter(const VulkanVolumeRendererExtDeleter& ) noexcept = default;
    void operator()(VulkanVolumeRendererExt* ptr) const noexcept{
        ptr->destroy();
    }
};

}

MRAYNS_END

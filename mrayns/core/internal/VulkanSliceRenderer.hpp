//
// Created by wyz on 2022/3/3.
//
#pragma once
#include "VulkanUtil.hpp"
#include "../Renderer.hpp"
MRAYNS_BEGIN

namespace internal{

class VulkanSliceRenderer:public SliceRenderer{
  public:

    void setVolume(const Volume&) override;
    Type getRendererType() const override;
    const Framebuffer& getFrameBuffers() const override;
    void render(const Slice&) override;
    void render(const SliceExt& slice,RenderType type) override;
    void setTransferFunction(const TransferFunction&) override;
    void updatePageTable(const std::vector<PageTableItem>&) override;

    static VulkanSliceRenderer* Create(VulkanNodeSharedResourceWrapper*);

    friend class VulkanSliceRendererDeleter;
    friend class VulkanRendererDeleter;
  private:

    void destroy();
    ~VulkanSliceRenderer() override;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class VulkanSliceRendererDeleter{
  public:
    constexpr VulkanSliceRendererDeleter() noexcept = default;
    VulkanSliceRendererDeleter(const VulkanSliceRendererDeleter&) noexcept = default;
    void operator()(VulkanSliceRenderer* ptr) const noexcept{
        ptr->destroy();
    }
};

}

MRAYNS_END
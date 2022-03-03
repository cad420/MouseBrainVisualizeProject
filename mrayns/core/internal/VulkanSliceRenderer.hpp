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
    Type getRendererType() const override;
    const Framebuffer& getFrameBuffers() const override;
    void render(const Slice&) override;

    static VulkanSliceRenderer* Create(VulkanNodeSharedResourceWrapper*);
    void destroy();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}

MRAYNS_END
//
// Created by wyz on 2022/2/25.
//
#pragma once
#include "../common/Image.hpp"

MRAYNS_BEGIN

class Framebuffer{
  public:
    Image& getColors() {return colors;}
    Image& getDepths() {return depths;}
    const Image& getColors() const {return colors;}
    const Image& getDepths() const {return depths;}
    Framebuffer()
        :colors(),depths(){}

    Framebuffer(int w,int h)
    :colors(w,h),depths(w,h)
    {}

    Framebuffer(Framebuffer&& rhs) noexcept
    :colors(std::move(rhs.colors)),depths(std::move(rhs.depths))
    {}

    Framebuffer& operator=(Framebuffer&& rhs) noexcept{
        new(this) Framebuffer(std::move(rhs));
        return *this;
    }
  private:
    Image colors;
    Image depths;
};

MRAYNS_END
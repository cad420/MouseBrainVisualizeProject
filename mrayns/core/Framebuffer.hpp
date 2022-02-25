//
// Created by wyz on 2022/2/25.
//
#pragma once
#include "../common/Image.hpp"

MRAYNS_BEGIN

class Framebuffer{
  public:
    const Image& getColors() const {return colors;}
    const Image& getDepths() const {return depths;}
  private:
    Image colors;
    Image depths;
};

MRAYNS_END
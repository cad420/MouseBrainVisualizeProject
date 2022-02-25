//
// Created by wyz on 2022/2/25.
//
#include "Renderer.hpp"
MRAYNS_BEGIN

/**
 * 1.因为render传入的slice的图片尺寸是各不相同的 所以需要创建一个存储多个尺寸不同的Framebuffer的缓存池 可以使用LRU策略
 *
 */
class SliceRendererImpl: public SliceRenderer{
  public:

};


MRAYNS_END
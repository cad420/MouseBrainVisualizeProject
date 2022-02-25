//
// Created by wyz on 2022/2/24.
//
#pragma once
#include <memory>
#include "Slice.hpp"
#include "GPUResource.hpp"
#include "../geometry/Camera.hpp"
#include "../common/Image.hpp"
#include "Framebuffer.hpp"
MRAYNS_BEGIN
/**
 * @brief Use vulkan to imply render.
 * Interface will not be related with any vulkan objects.
 * Renderer is just render according to Slice or Camera param with current GPUResource,
 * it will not change any GPUResource but some self resource lick shader uniform variables
 * so a renderer for multi-thread call to render is not safe or correct.
 */
class Renderer{
  public:
    enum Type:int{
        SLICE = 0,VOLUME=1
    };
    virtual Type getRendererType() const = 0;
    virtual const Framebuffer& getFrameBuffers() const = 0;
};

/**
 * Both SliceRenderer and VolumeRenderer's render() call is sync method, that meanings
 * it may cost different time according to render task schedule and memory situation.
 * No async for render because render and decode will influence each other's efficiency.
 *
 */

class SliceRenderer: public Renderer{
  public:
    virtual ~SliceRenderer() = default;
    //create will register the renderer into GPUResource
    static std::unique_ptr<SliceRenderer> create(GPUResource&);
    /**
     * @param slice
     * @note slice's image size is different for each render call with a high probability.
     */
    virtual void render(const Slice& slice) = 0;

};


class VolumeRenderer: public Renderer{
  public:
    static std::unique_ptr<VolumeRenderer> create(GPUResource&);
    virtual void render(const Camera&) = 0;

};


template <Renderer::Type type>
struct RendererCaster;


template <>
struct RendererCaster<Renderer::SLICE>{
    static SliceRenderer* GetPtr(Renderer* p){
        return dynamic_cast<SliceRenderer*>(p);
    }
};

template <>
struct RendererCaster<Renderer::VOLUME>{
    static VolumeRenderer* GetPtr(Renderer* p){
        return dynamic_cast<VolumeRenderer*>(p);
    }
};

MRAYNS_END
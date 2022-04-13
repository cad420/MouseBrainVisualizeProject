//
// Created by wyz on 2022/2/24.
//
#pragma once
#include <memory>
#include "Slice.hpp"
#include "../geometry/Camera.hpp"
#include "../common/Image.hpp"
#include "Framebuffer.hpp"
#include "PageTable.hpp"
MRAYNS_BEGIN

struct TransferFunction{
    using Point = std::pair<float,Vector4f>;
    std::vector<Point> points;
};
struct TransferFunctionExt1D: public TransferFunction{
    static constexpr int TFDim = 256;
    float tf[TFDim*4];
};

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
    virtual void setVolume(Volume)  = 0;
    //not necessary for slice render
    virtual void setTransferFunction(TransferFunction) {};
    using PageTableItem = std::pair<PageTable::EntryItem,PageTable::ValueItem>;
    virtual void updatePageTable(const std::vector<PageTableItem>&){}
    virtual Type getRendererType() const = 0;
    virtual const Framebuffer& getFrameBuffers() const = 0;
    virtual ~Renderer() = default;
};

/**
 * Both SliceRenderer and VolumeRenderer's render() call is sync method, that meanings
 * it may cost different time according to render task schedule and memory situation.
 * No async for render because render and decode will influence each other's efficiency.
 *
 */

class SliceRenderer: public Renderer{
  public:
    //slice renderer will create fixed framebuffer size
    static constexpr int MaxSliceW = 1920;
    static constexpr int MaxSliceH = 1080;

    /**
     * @param slice
     * @note slice's image size is different for each render call with a high probability.
     */
    virtual void render(const Slice& slice) = 0;

    enum RenderType:uint32_t{
        MIP = 0,
        RAYCAST = 1,
    };

    virtual void render(const SliceExt& slice,RenderType type){}
  protected:
    virtual ~SliceRenderer() = default;
};


class VolumeRenderer: public Renderer{
  public:

    virtual void render(const VolumeRendererCamera&) = 0;

    virtual bool renderPass(const VolumeRendererCamera&,bool newFrame){ return true;}

    //todo change pure virtual to virtual
    virtual void setTransferFunction(TransferFunctionExt1D) = 0;
  protected:
    virtual ~VolumeRenderer() = default;
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
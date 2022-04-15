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

    virtual ~Renderer() = default;

    virtual void setVolume(const Volume&)  = 0;

    virtual Type getRendererType() const = 0;

    virtual const Framebuffer& getFrameBuffers() const = 0;

    //not necessary for slice render
    virtual void setTransferFunction(const TransferFunction&) {};

    using PageTableItem = std::pair<PageTable::EntryItem,PageTable::ValueItem>;
    virtual void updatePageTable(const std::vector<PageTableItem>&){}

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

    virtual void render(const Slice& slice) = 0;

    enum RenderType:uint32_t{
        MIP = 0,
        RAYCAST = 1,
    };
    /**
     * @brief this function extend slice render method, slice may has depth
     * can perform a raycast render.
     * @note if choose caycast render type, called should set transfer function first,
     * and raycast is not suitable for slice with large depth because of fixed lod policy.
     */
    virtual void render(const SliceExt& slice,RenderType type){}

  protected:
    virtual ~SliceRenderer() = default;
};


class VolumeRenderer: public Renderer{
  public:
    void setTransferFunction(const TransferFunction&) override = 0;

    virtual void setTransferFunction(const TransferFunctionExt1D&){};

    virtual void render(const VolumeRendererCamera&) = 0;

    /**
     * @brief using for multi render passes out-of-core volume render.
     * @param newFrame meanings whether this render pass should clear color and depth buffer
     * and use the new passed camera to begin a framebuffer.
     * @return whether out-of-core volume render is finished.
     * @note to render a complete framebuffer this function should be called in one thread
     * at same time until it return true.
     */
    virtual bool renderPass(const VolumeRendererCamera&,bool newFrame){ return true;}

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
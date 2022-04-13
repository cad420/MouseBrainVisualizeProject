//
// Created by wyz on 2022/4/13.
//
#include "SliceHelper.hpp"
#include <omp.h>
MRAYNS_BEGIN


void SliceHelper::UniformMergeSlice(const std::vector<SliceExt>& subSlices,std::vector<const Image*> subColors,Image& result)
{
#ifndef NDEBUG
    bool ok = true;
    int w = result.width();
    int h = result.height();
    for(const auto& slice:subSlices){
        if(slice.n_pixels_w != w || slice.n_pixels_h != h){
            ok = false;
            break;
        }
    }
    assert(ok);
    for(const auto img:subColors){
        if(img->width() != w || img->height() != h){
            ok = false;
            break;
        }
    }
    assert(ok);
    assert(subSlices.size() == subColors.size());
#endif

    for(int i = 0; i < subSlices.size(); i++){
        const auto& slice = subSlices[i];
        const auto img = subColors[i];
        if(!img) continue;
        int sx = slice.region.min_x;
        int sy = slice.region.min_y;
        int dx = slice.region.max_x;
        int dy = slice.region.max_y;
#pragma omp parallel for
        for(int x = sx; x <= dx; x++){
            for(int y = sy; y <= dy; y++){
                result(x,y) = (*img)(x,y);
            }
        }
    }
}

MRAYNS_END
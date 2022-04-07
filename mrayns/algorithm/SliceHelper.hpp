//
// Created by wyz on 2022/2/25.
//
#pragma once
#include "../core/Slice.hpp"
#include <cmath>
MRAYNS_BEGIN

struct SliceHelper{
    static bool IsSubSlice(const Slice& slice){
        return !(slice.region.min_x==0 && slice.region.min_y==0
                 && slice.region.max_x == slice.n_pixels_w - 1 && slice.region.max_y == slice.n_pixels_h - 1);
    }
    static int GetSliceLod(const Slice& slice){
        if(slice.voxels_per_pixel < 1.f) return 0;
        return static_cast<int>(std::floor(std::log2(slice.voxels_per_pixel)));
    }
    inline static float SliceStepVoxelRatio = 0.5f;
};

MRAYNS_END
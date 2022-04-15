//
// Created by wyz on 2022/2/23.
//
#pragma once

#include "../geometry/Plane.hpp"

MRAYNS_BEGIN

/**
 * 在Manager处对Slice进行基于图像空间的划分 比如四叉树或者二维KD-Tree 划分后的Slice有着不同的w和h
 * 这个w和h对应n_pixels_w和n_pixels_h 渲染时需要将Viewport转换为(0,0,n_pixels_w,n_pixels_h)
 * region是在Node上划分的结果 可以是sort first 或者sort last
 */
struct Slice{
    size_t id;
    Rect region;
    int n_pixels_w;
    int n_pixels_h;

    Vector3f origin;//center of slice
    Vector3f normal;
    Vector3f x_dir;//left -> right
    Vector3f y_dir;//top -> bottom
    float voxels_per_pixel;
//    static constexpr int MaxLod = 12;
//    float lod_voxel[MaxLod];

};

struct SliceExt: public Slice{
    int lod;//this must compute by call defined function in the library
    float step;//this should compute according to voxel
    float depth = 0.f;//this should based on voxel
};

MRAYNS_END

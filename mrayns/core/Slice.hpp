//
// Created by wyz on 2022/2/23.
//
#pragma once

#include "../geometry/Plane.hpp"

MRAYNS_BEGIN

struct Slice{
    size_t id;
    Rect region;
    int n_pixels_w;
    int n_pixels_h;

    Vector3f origin;//top left corner
    Vector3f normal;
    Vector3f x_dir;
    Vector3f y_dir;
    float voxels_per_pixel;

};

MRAYNS_END

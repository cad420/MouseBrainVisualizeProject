//
// Created by wyz on 2022/2/25.
//
#pragma once

#include "../common/MathTypes.hpp"
MRAYNS_BEGIN

struct Rect{
    int min_x;
    int min_y;
    int max_x;
    int max_y;
};

// Ax + By + Cz + D = 0
struct Plane{
    Vector3f normal;
    float D;
};

MRAYNS_END

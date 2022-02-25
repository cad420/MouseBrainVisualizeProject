//
// Created by wyz on 2022/2/24.
//
#pragma once

#include "Frustum.hpp"
MRAYNS_BEGIN

struct Camera{
    Vector3f position;
    Vector3f target;
    Vector3f up;
    float near_z;
    float far_z;
    int width;
    int height;
};

MRAYNS_END

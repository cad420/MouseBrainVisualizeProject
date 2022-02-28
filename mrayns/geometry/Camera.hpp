//
// Created by wyz on 2022/2/24.
//
#pragma once

#include "Frustum.hpp"
MRAYNS_BEGIN

struct Camera{
    static constexpr Vector3f WorldUp = Vector3f{0.f,1.f,0.f};
    Vector3f position;
    Vector3f target;
    Vector3f up;
    float near_z;
    float far_z;
    float fov{30.f};
    int width;
    int height;
};

struct CameraExt: public Camera{
    float aspect;
    Vector3f right;
    Vector3f front;
    float yaw;
    float pitch;
};

MRAYNS_END

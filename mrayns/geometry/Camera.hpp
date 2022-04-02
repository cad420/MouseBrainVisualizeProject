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
    float far_z;//尽可能大 当相机在体外的时候 最大的lod也被使用
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

struct VolumeRendererLodDist{
    static constexpr int MaxLod = 12;
    float lod_dist[MaxLod] = {std::numeric_limits<float>::max()};
};
struct VolumeRendererSortFirstDesc{

};
struct VolumeRendererSortLastDesc{

};
struct VolumeRendererMPIDesc{

};
struct VolumeRendererCamera: public Camera{
    float raycasting_step;
    VolumeRendererLodDist lod_dist;
    VolumeRendererMPIDesc mpi_desc;
};

MRAYNS_END

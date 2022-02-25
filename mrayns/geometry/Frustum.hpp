//
// Created by wyz on 2022/2/25.
//
#pragma once
#include "Plane.hpp"

MRAYNS_BEGIN

struct BoundBox{
    Vector3f min_p;
    Vector3f max_p;
};

enum class BoxVisibility{
    Invisible,
    Intersecting,
    FullyVisible
};

struct Frustum{
    enum PLANE_IDX : uint32_t
    {
        LEFT_PLANE_IDX   = 0,
        RIGHT_PLANE_IDX  = 1,
        BOTTOM_PLANE_IDX = 2,
        TOP_PLANE_IDX    = 3,
        NEAR_PLANE_IDX   = 4,
        FAR_PLANE_IDX    = 5,
        NUM_PLANES       = 6
    };

    Plane left_plane;
    Plane right_plane;
    Plane bottom_plane;
    Plane top_plane;
    Plane near_plane;
    Plane far_plane;

    const Plane& getPlane(PLANE_IDX Idx) const
    {

        const Plane* Planes = reinterpret_cast<const Plane*>(this);
        return Planes[static_cast<size_t>(Idx)];
    }

    Plane& getPlane(PLANE_IDX Idx)
    {
        Plane* Planes = reinterpret_cast<Plane*>(this);
        return Planes[static_cast<size_t>(Idx)];
    }
};

MRAYNS_END

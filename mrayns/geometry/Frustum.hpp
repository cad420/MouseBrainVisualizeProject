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
inline bool Contain(const BoundBox& box,const Vector3f& point){
    return box.min_p.x <= point.x && point.x <= box.max_p.x
        && box.min_p.y <= point.y && point.y <= box.max_p.y
        && box.min_p.z <= point.z && point.z <= box.max_p.z;
}
inline BoundBox UnionBoundBox(const BoundBox& b1,const BoundBox& b2){
    return BoundBox{
        {
            std::min(b1.min_p.x,b2.min_p.x),
            std::min(b1.min_p.y,b2.min_p.y),
            std::min(b1.min_p.z,b2.min_p.z)
        },
        {
            std::max(b1.max_p.x,b2.max_p.x),
            std::max(b1.max_p.y,b2.max_p.y),
            std::max(b1.max_p.z,b2.max_p.z)
        }
    };
}
template <typename... T>
BoundBox UnionBoundBoxes(const BoundBox& b1,const BoundBox& b2,T&&... others){
    return UnionBoundBoxes(UnionBoundBox(b1,b2),others...);
}

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

struct FrustumExt: public Frustum{
    Vector3f frustum_corners[8];
};

MRAYNS_END

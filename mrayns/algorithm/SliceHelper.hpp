//
// Created by wyz on 2022/2/25.
//
#pragma once
#include "../core/Slice.hpp"
#include "../geometry/Frustum.hpp"
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

    static void ExtractViewFrustumFromSlice(const SliceExt& slice,Frustum& frustum,float voxel){
        frustum.near_plane.normal = -slice.normal;
        Vector3f near_plane_center = slice.origin + slice.normal * slice.depth * 0.5f;
        frustum.near_plane.D = - dot(frustum.near_plane.normal,near_plane_center);

        frustum.far_plane.normal = slice.normal;
        Vector3f far_plane_center = slice.origin - slice.normal * slice.depth * 0.5f;
        frustum.far_plane.D = -dot(frustum.far_plane.normal,far_plane_center);

        frustum.top_plane.normal = slice.y_dir;
        Vector3f top_plane_center = slice.origin - slice.y_dir * slice.voxels_per_pixel * (float)slice.n_pixels_h * 0.5f * voxel;
        frustum.top_plane.D = -dot(frustum.top_plane.normal,top_plane_center);

        frustum.bottom_plane.normal = -slice.y_dir;
        Vector3f bottom_plane_center = slice.origin + slice.y_dir * slice.voxels_per_pixel * (float)slice.n_pixels_h * 0.5f * voxel;
        frustum.bottom_plane.D = -dot(frustum.bottom_plane.normal,bottom_plane_center);

        frustum.left_plane.normal = slice.x_dir;
        Vector3f left_plane_center = slice.origin - slice.x_dir * slice.voxels_per_pixel * (float)slice.n_pixels_w * 0.5f * voxel;
        frustum.left_plane.D = -dot(frustum.left_plane.normal,left_plane_center);

        frustum.right_plane.normal = -slice.x_dir;
        Vector3f right_plane_center = slice.origin + slice.x_dir * slice.voxels_per_pixel * (float)slice.n_pixels_w * 0.5f * voxel;
        frustum.right_plane.D = -dot(frustum.right_plane.normal,right_plane_center);
    }

    static void ExtractViewFrustumExtFromSliceExt(const SliceExt& slice,FrustumExt& frustumExt,float voxel){
        ExtractViewFrustumFromSlice(slice,static_cast<Frustum&>(frustumExt),voxel);
        static const Vector3f corners[8] = {
            Vector3f{-1.f,-1.f,-1.f},
            Vector3f{1.f,-1.f,-1.f},
            Vector3f{-1.f,1.f,-1.f},
            Vector3f{1.f,1.f,-1.f},
            Vector3f{-1.f,-1.f,1.f},
            Vector3f{1.f,-1.f,1.f},
            Vector3f{-1.f,1.f,1.f},
            Vector3f{1.f,1.f,1.f}
        };
        for(int i = 0 ; i < 8 ; i++){
            frustumExt.frustum_corners[i] = slice.origin
                + corners[i].x * slice.x_dir * slice.voxels_per_pixel * (float)slice.n_pixels_w * 0.5f * voxel
                - corners[i].y * slice.y_dir * slice.voxels_per_pixel * (float)slice.n_pixels_h * 0.5f * voxel
                - corners[i].z * slice.normal * slice.depth * 0.5f;
        }
    }

};

MRAYNS_END
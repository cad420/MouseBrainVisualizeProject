//
// Created by wyz on 2022/2/25.
//
#pragma once
#include "../core/Slice.hpp"
#include "../geometry/Frustum.hpp"
#include "../common/Image.hpp"
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
        Vector3f slice_region_center = slice.origin - slice.x_dir * slice.voxels_per_pixel * (float)slice.n_pixels_w * 0.5f * voxel
            -slice.y_dir * slice.voxels_per_pixel * (float)slice.n_pixels_h * 0.5f * voxel;
        float slice_region_w = slice.region.max_x - slice.region.min_x;
        float slice_region_h = slice.region.max_y - slice.region.min_y;
        slice_region_center += slice.x_dir * slice.voxels_per_pixel * (float)(slice.region.max_x + slice.region.min_x) * 0.5f * voxel
                               + slice.y_dir * slice.voxels_per_pixel * (float)(slice.region.max_y + slice.region.min_y) * 0.5f * voxel;
        frustum.near_plane.normal = -slice.normal;
        Vector3f near_plane_center = slice_region_center + slice.normal * slice.depth * 0.5f;
        frustum.near_plane.D = - dot(frustum.near_plane.normal,near_plane_center);

        frustum.far_plane.normal = slice.normal;
        Vector3f far_plane_center = slice_region_center - slice.normal * slice.depth * 0.5f;
        frustum.far_plane.D = -dot(frustum.far_plane.normal,far_plane_center);

        frustum.top_plane.normal = slice.y_dir;
        Vector3f top_plane_center = slice_region_center - slice.y_dir * slice.voxels_per_pixel * slice_region_h * 0.5f * voxel;
        frustum.top_plane.D = -dot(frustum.top_plane.normal,top_plane_center);

        frustum.bottom_plane.normal = -slice.y_dir;
        Vector3f bottom_plane_center = slice_region_center + slice.y_dir * slice.voxels_per_pixel * slice_region_h * 0.5f * voxel;
        frustum.bottom_plane.D = -dot(frustum.bottom_plane.normal,bottom_plane_center);

        frustum.left_plane.normal = slice.x_dir;
        Vector3f left_plane_center = slice_region_center - slice.x_dir * slice.voxels_per_pixel * slice_region_w * 0.5f * voxel;
        frustum.left_plane.D = -dot(frustum.left_plane.normal,left_plane_center);

        frustum.right_plane.normal = -slice.x_dir;
        Vector3f right_plane_center = slice_region_center + slice.x_dir * slice.voxels_per_pixel * slice_region_w * 0.5f * voxel;
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
        Vector3f slice_region_center = slice.origin - slice.x_dir * slice.voxels_per_pixel * (float)slice.n_pixels_w * 0.5f * voxel
                                       -slice.y_dir * slice.voxels_per_pixel * (float)slice.n_pixels_h * 0.5f * voxel;
        float slice_region_w = slice.region.max_x - slice.region.min_x;
        float slice_region_h = slice.region.max_y - slice.region.min_y;
        slice_region_center += slice.x_dir * slice.voxels_per_pixel * (float)(slice.region.max_x + slice.region.min_x) * 0.5f * voxel
                               + slice.y_dir * slice.voxels_per_pixel * (float)(slice.region.max_y + slice.region.min_y) * 0.5f * voxel;
        for(int i = 0 ; i < 8 ; i++){
            frustumExt.frustum_corners[i] = slice_region_center
                + corners[i].x * slice.x_dir * slice.voxels_per_pixel * slice_region_w * 0.5f * voxel
                - corners[i].y * slice.y_dir * slice.voxels_per_pixel * slice_region_h * 0.5f * voxel
                - corners[i].z * slice.normal * slice.depth * 0.5f;
        }
    }

    static void UniformDivideSlice(const SliceExt& slice,int n,std::vector<SliceExt>& subSlices){
        if(n == 0) return;
        if(n == 1) {
            subSlices.emplace_back(slice);
            return;
        }
        int w = slice.n_pixels_w;
        int h = slice.n_pixels_h;
        if(w > h){
            SliceExt left_slice = slice;
            left_slice.region.max_x = (left_slice.region.min_x + left_slice.region.max_x) / 2;
            SliceExt right_slice = slice;
            right_slice.region.min_x = (right_slice.region.min_x + right_slice.region.max_x + 1) / 2;
            std::vector<SliceExt> left_slices;
            std::vector<SliceExt> right_slices;
            UniformDivideSlice(left_slice,n/2,left_slices);
            UniformDivideSlice(right_slice,(n+1)/2,right_slices);
            left_slices.insert(left_slices.end(),right_slices.begin(),right_slices.end());
            subSlices = std::move(left_slices);
        }
        else{
            SliceExt top_slice = slice;
            top_slice.region.max_y = (top_slice.region.min_y + top_slice.region.max_y) / 2;
            SliceExt bottom_slice = slice;
            bottom_slice.region.min_y = (bottom_slice.region.min_y + bottom_slice.region.max_y + 1) / 2;
            std::vector<SliceExt> top_slices;
            std::vector<SliceExt> bottom_slices;
            UniformDivideSlice(top_slice,n/2,top_slices);
            UniformDivideSlice(bottom_slice,(n+1)/2,bottom_slices);
            top_slices.insert(top_slices.end(),bottom_slices.begin(),bottom_slices.end());
            subSlices = std::move(top_slices);
        }
    }
    static void UniformMergeSlice(const std::vector<SliceExt>& subSlices,std::vector<const Image*> subColors,Image& result);


};

MRAYNS_END
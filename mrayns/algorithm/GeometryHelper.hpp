//
// Created by wyz on 2022/2/25.
//

#pragma once
#include "../geometry/Camera.hpp"

MRAYNS_BEGIN

struct GeometryHelper{

    static Matrix4f ExtractViewMatrixFromCamera(const Camera& camera){
        return lookAt(camera.position,camera.target,camera.up);
    }

    static Matrix4f ExtractProjMatrixFromCamera(const Camera& camera){
        return perspective(radians(camera.fov*0.5f),static_cast<float>(camera.width)/static_cast<float>(camera.height),camera.near_z,camera.far_z);
    }


// For OpenGL, matrix is still considered row-major. The only difference is that
// near clip plane is at -1, not 0.
//
// Note that returned plane normal vectors are not normalized, which does not make a difference
// when testing a point against the plane:
//
//      IsPointInsidePlane = dot(Plane.Normal, Point) < Plane.Distance
//
// However, to use the planes with other distances (e.g. for testing a sphere against the plane),
// the normal vectors must be normalized and the distances scaled accordingly.
    static void ExtractViewFrustumPlanesFromMatrix(const Matrix4f& matrix, Frustum& frustum, bool is_OpenGL = false)
    {
        // For more details, see Gribb G., Hartmann K., "Fast Extraction of Viewing Frustum Planes from the
        // World-View-Projection Matrix" (the paper is available at
        // http://gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf)

        // Left clipping plane
        frustum.left_plane.normal.x = matrix[0][3] + matrix[0][0];
        frustum.left_plane.normal.y = matrix[1][3] + matrix[1][0];
        frustum.left_plane.normal.z = matrix[2][3] + matrix[2][0];
        frustum.left_plane.D = matrix[3][3] + matrix[3][0];

        // Right clipping plane
        frustum.right_plane.normal.x = matrix[0][3] - matrix[0][0];
        frustum.right_plane.normal.y = matrix[1][3] - matrix[1][0];
        frustum.right_plane.normal.z = matrix[2][3] - matrix[2][0];
        frustum.right_plane.D = matrix[3][3] - matrix[3][0];

        // Top clipping plane
        frustum.top_plane.normal.x = matrix[0][3] - matrix[0][1];
        frustum.top_plane.normal.y = matrix[1][3] - matrix[1][1];
        frustum.top_plane.normal.z = matrix[2][3] - matrix[2][1];
        frustum.top_plane.D = matrix[3][3] - matrix[3][1];

        // Bottom clipping plane
        frustum.bottom_plane.normal.x = matrix[0][3] + matrix[0][1];
        frustum.bottom_plane.normal.y = matrix[1][3] + matrix[1][1];
        frustum.bottom_plane.normal.z = matrix[2][3] + matrix[2][1];
        frustum.bottom_plane.D = matrix[3][3] + matrix[3][1];

        // Near clipping plane
        if (is_OpenGL)
        {
            // -w <= z <= w
            frustum.near_plane.normal.x = matrix[0][3] + matrix[0][2];
            frustum.near_plane.normal.y = matrix[1][3] + matrix[1][2];
            frustum.near_plane.normal.z = matrix[2][3] + matrix[2][2];
            frustum.near_plane.D = matrix[3][3] + matrix[3][2];
        }
        else
        {
            // 0 <= z <= w
            frustum.near_plane.normal.x = matrix[0][2];
            frustum.near_plane.normal.y = matrix[1][2];
            frustum.near_plane.normal.z = matrix[2][2];
            frustum.near_plane.D = matrix[3][2];
        }

        // Far clipping plane
        frustum.far_plane.normal.x = matrix[0][3] - matrix[0][2];
        frustum.far_plane.normal.y = matrix[1][3] - matrix[1][2];
        frustum.far_plane.normal.z = matrix[2][3] - matrix[2][2];
        frustum.far_plane.D = matrix[3][3] - matrix[3][2];
    }

    static void ExtractViewFrustumPlanesFromMatrix(const Matrix4f& matrix, FrustumExt& frustumExt, bool is_OpenGL = false){
        ExtractViewFrustumPlanesFromMatrix(matrix,static_cast<Frustum&>(frustumExt),is_OpenGL);

        Matrix4f  inv_matrix = inverse(matrix);
        auto id = matrix * inv_matrix;
        float near_clip_z = is_OpenGL ? -1.f : 0.f;
        static const Vector3f proj_space_corners[] = {
            Vector3f(-1,-1,near_clip_z),
            Vector3f(1,-1,near_clip_z),
            Vector3f(-1,1,near_clip_z),
            Vector3f(1,1,near_clip_z),

            Vector3f(-1,-1,1),
            Vector3f(1,-1,1),
            Vector3f(-1,1,1),
            Vector3f(1,1,1)
        };
        for(int i = 0 ; i < 8 ; i++){
            //千万别乘反了
            auto t = inv_matrix * Vector4f(proj_space_corners[i],1.f) ;
            frustumExt.frustum_corners[i] = Vector3f(t.x / t.w, t.y / t.w, t.z / t.w);
        }
    }

    static BoxVisibility GetBoxVisibilityAgainstPlane(const Plane& plane,
                                                      const BoundBox& box){
        const auto& normal = plane.normal;
        Vector3f max_point{
            (normal.x > 0.f) ? box.max_p.x : box.min_p.x,
            (normal.y > 0.f) ? box.max_p.y : box.min_p.y,
            (normal.z > 0.f) ? box.max_p.z : box.min_p.z
        };
        float d_max = dot(max_point,normal) + plane.D;
        if(d_max < 0.f) return BoxVisibility::Invisible;
        Vector3f min_point{
            (normal.x > 0.f) ? box.min_p.x : box.max_p.x,
            (normal.y > 0.f) ? box.min_p.y : box.max_p.y,
            (normal.z > 0.f) ? box.min_p.z : box.max_p.z
        };
        float d_min = dot(min_point,normal) + plane.D;
        if(d_min > 0.f) return BoxVisibility::FullyVisible;

        return BoxVisibility::Intersecting;
    }

    static BoxVisibility GetBoxVisibility(const Frustum& frustum,
                                          const BoundBox& box){
        uint32_t num_planes_inside = 0;
        for(uint32_t plane_idx = 0;plane_idx < Frustum::NUM_PLANES; plane_idx++){
            const auto& cur_plane = frustum.getPlane(static_cast<Frustum::PLANE_IDX>(plane_idx));

            auto visibility_against_plane = GetBoxVisibilityAgainstPlane(cur_plane,box);

            if(visibility_against_plane == BoxVisibility::Invisible)
                return BoxVisibility::Invisible;

            if(visibility_against_plane == BoxVisibility::FullyVisible)
                num_planes_inside++;
        }
        return (num_planes_inside == Frustum::NUM_PLANES) ? BoxVisibility::FullyVisible : BoxVisibility::Intersecting ;
    }

    static BoxVisibility GetBoxVisibility(const FrustumExt& frustumExt,
                                          const BoundBox& box){
        auto visibility = GetBoxVisibility(static_cast<const Frustum&>(frustumExt),box);
        if(visibility == BoxVisibility::FullyVisible || visibility == BoxVisibility::Invisible)
            return visibility;

        // Additionally test if the whole frustum is outside one of
        // the the bounding box planes. This helps in the following situation:
        //
        //
        //       .
        //      /   '  .       .
        //     / AABB  /   . ' |
        //    /       /. '     |
        //       ' . / |       |
        //       * .   |       |
        //           ' .       |
        //               ' .   |
        //                   ' .

        // Test all frustum corners against every bound box plane
        for(int i_box_plane = 0; i_box_plane < 6; ++i_box_plane){
            float cur_plane_coord = reinterpret_cast<const float*>(&box)[i_box_plane];
            int i_coord_order = i_box_plane % 3;// 0 1 2 min_p  0 1 2 max_p
            float f_sign = (i_box_plane >= 3) ? +1.f : -1.f;//first three planes are yz(x) xz(y) xy(z)
            bool all_corners_outside = true;
            for(int i_corner = 0; i_corner < 8; i_corner++){
                float cur_corner_coord = frustumExt.frustum_corners[i_corner][i_coord_order];
                if(f_sign * (cur_plane_coord - cur_corner_coord) > 0){
                    all_corners_outside = false;
                    break;
                }
            }
            if(all_corners_outside)
                return BoxVisibility::Invisible;
        }
        return BoxVisibility::Intersecting;
    }

    static bool TestBoxValid(const BoundBox& box){
        return box.min_p.x <= box.max_p.x && box.min_p.y <= box.max_p.y && box.min_p.z <= box.max_p.z;
    }
    static BoundBox GetBoxIntersection(const BoundBox& b1,const BoundBox& b2){
        return BoundBox{
            {
                std::max(b1.min_p.x,b2.min_p.x),
                std::max(b1.min_p.y,b2.min_p.y),
                std::max(b1.min_p.z,b2.min_p.z)
            },
            {
                std::min(b1.max_p.x,b2.max_p.x),
                std::min(b1.max_p.y,b2.max_p.y),
                std::min(b1.max_p.z,b2.max_p.z)
            }
        };
    }
    static bool TestBoxIntersect(const BoundBox& b1,const BoundBox& b2){
        assert(TestBoxValid(b1) && TestBoxValid(b2));
        return TestBoxValid(GetBoxIntersection(b1,b2));
    }
    //test if b1 entirely contain b2
    static bool TestBoxContain(const BoundBox& b1,const BoundBox& b2){
        assert(TestBoxValid(b1) && TestBoxValid(b2));
        return b1.min_p.x <= b2.min_p.x && b1.min_p.y <= b2.min_p.y && b1.min_p.z <= b2.min_p.z
        && b1.max_p.x >= b2.max_p.x && b1.max_p.y >= b2.max_p.y && b1.max_p.z >= b2.max_p.z;
    }
    static BoxVisibility GetBoxVisibility(const BoundBox& frustum,const BoundBox& box){
        if(TestBoxValid(GetBoxIntersection(frustum,box))){
            if(TestBoxContain(frustum,box)){
                return BoxVisibility::FullyVisible;
            }
            else{
                return BoxVisibility::Intersecting;
            }
        }
        else{
            return BoxVisibility::Invisible;
        }
    }
};

MRAYNS_END
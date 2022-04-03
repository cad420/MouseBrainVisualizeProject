//
// Created by wyz on 2022/4/2.
//
#pragma once
#include "../core/Volume.hpp"

MRAYNS_BEGIN

struct RenderHelper{

    static void GetDefaultLodDist(const Volume& volume,
                                  float* lod_dist,
                                  int max_lod){
        float virtual_block_length = volume.getBlockLengthWithoutPadding();
        auto volume_space = volume.getVolumeSpace();
        auto virtual_block_length_space = volume_space * virtual_block_length;
        auto base_lod_dist = length(virtual_block_length_space) * 0.5f;
        for(int i = 0; i < max_lod; i++){
            lod_dist[i] = base_lod_dist * (1 << i);
        }
        lod_dist[max_lod] = std::numeric_limits<float>::max();
    }



};


MRAYNS_END

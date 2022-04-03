//
// Created by wyz on 2022/4/2.
//

#pragma once
#include "../common/Define.hpp"
#include "../core/Renderer.hpp"
MRAYNS_BEGIN

inline void ComputeTransferFunction1DExt(TransferFunctionExt1D& tf){
    assert(!tf.points.empty());
    std::vector<uint8_t> keys;
    for(auto& p:tf.points){
        keys.emplace_back(p.first * 255);
    }
    for(int i = 0; i < keys.front(); i++){
        for(int j = 0; j < 4; j++){
            tf.tf[i * 4 + j] = tf.points.front().second[j];
        }
    }
    for(int i = keys.back(); i < 256; i++){
        for(int j = 0; j < 4; j++){
            tf.tf[i * 4 + j] = tf.points.back().second[j];
        }
    }
    for(int i = 1;i < keys.size(); i++){
        int left = keys[i - 1], right = keys[i];
        auto left_color = tf.points[i-1].second;
        auto right_color = tf.points[i].second;
        for(int j = left; j <= right; j++){
            for(int k = 0; k < 4; k++){
                tf.tf[j * 4 + k] = 1.f * (j - left) / (right - left) * right_color[k] +
                                   1.f * (right - j) / (right - left) * left_color[k];
            }
        }
    }
}

MRAYNS_END

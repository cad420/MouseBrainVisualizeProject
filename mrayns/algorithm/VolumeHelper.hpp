//
// Created by wyz on 2022/4/16.
//
#pragma once
#include "../core/Volume.hpp"

MRAYNS_BEGIN

struct VolumeHelper{
    using BlockIndex = Volume::BlockIndex;
    static Vector3i ComputeLodVolumeBlockDim(const Volume& volume,uint32_t lod){
        int lod_t = 1 << lod;
        Vector3i volume_dim = volume.getVolumeDim();
        Vector3i lod0_block_dim = (volume_dim + volume.getBlockLengthWithoutPadding() -1 ) / volume.getBlockLengthWithoutPadding();
        return (lod0_block_dim + lod_t - 1) / lod_t;
    }
    static void GetVolumeNeighborBlocks(const Volume& volume,const BlockIndex& blockIndex,std::vector<BlockIndex>& neighborBlocks){
        int lod = blockIndex.w;
        assert(lod >= 0);
        auto lod_block_dim = ComputeLodVolumeBlockDim(volume,lod);
        neighborBlocks.clear();
        auto isValidBlock = [&](const Vector3i & block_index){
            return block_index.x >=0 && block_index.y >= 0 && block_index.z >= 0
                   && block_index.x < lod_block_dim.x && block_index.y < lod_block_dim.y && block_index.z < lod_block_dim.z;
        };
        static Vector3i neighbors[6] = {
            {-1,0,0},
            {1,0,0},
            {0,-1,0},
            {0,1,0},
            {0,0,-1},
            {0,0,1}
        };
        Vector3i block_index = {blockIndex.x,blockIndex.y,blockIndex.z};
        for(int i = 0; i < 6; i++){
            auto n_block_index = block_index + neighbors[i];
            if(isValidBlock(n_block_index)){
                neighborBlocks.emplace_back(n_block_index.x,n_block_index.y,n_block_index.z,lod);
            }
        }
    }
    static BlockIndex GetNextLodBlockIndex(const BlockIndex& blockIndex){
        return BlockIndex{(blockIndex.x)/2,(blockIndex.y)/2,(blockIndex.z)/2,blockIndex.w+1};
    }
    static BlockIndex GetLodBlockIndex(const BlockIndex& blockIndex,int targetLod){
        if(blockIndex.w == targetLod) return blockIndex;
        if(targetLod < blockIndex.w){
            throw std::runtime_error("target lod should bigger than current lod");
        }
        return GetLodBlockIndex(GetNextLodBlockIndex(blockIndex),targetLod);
    }
    static bool VolumeSpacePositionInsideVolume(const Volume& volume,const Vector3f& position){
        auto volume_board= volume.getVolumeSpace() * volume.getVolumeDim();
        return position.x >= 0.f && position.y >= 0.f && position.z >= 0.f
        && position.x < volume_board.x && position.y < volume_board.y && position.z < volume_board.z;
    }
    static BlockIndex GetBlockIndexByVolumeSpacePosition(const Volume& volume,const Vector3f& pos,int lod){
        if(!VolumeSpacePositionInsideVolume(volume,pos)) return BlockIndex{};
        Vector3f voxel_pos = pos / volume.getVolumeSpace();
        float l = volume.getBlockLengthWithoutPadding() << lod;
        Vector3i block_index = voxel_pos / l;
        return BlockIndex {block_index.x,block_index.y,block_index.z,lod};
    }
    static float ComputeDistanceToBlockCenter(const Volume& volume,const BlockIndex& blockIndex,const Vector3f& pos){
        assert(blockIndex.isValid());
        int lod_t = 1 << blockIndex.w;
        float b = volume.getBlockLengthWithoutPadding() * lod_t;
        Vector3f block_center = {blockIndex.x+0.5f,blockIndex.y+0.5f,blockIndex.z+0.5f};
        block_center *= volume.getVolumeSpace() * b;
        return length(block_center-pos);
    }
};


MRAYNS_END

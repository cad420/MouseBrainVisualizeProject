//
// Created by wyz on 2022/2/23.
//

#pragma once
#include <string>
#include <type_traits>
#include "../common/Define.hpp"
#include "../common/Hash.hpp"
MRAYNS_BEGIN
/**
 * @brief Just some information about the volume, no real volume data stored.
 * Get volume data should @see IVolumeBlockProviderInterface.
 */
class Volume{
  public:
    enum VoxelType{
        UNKNOWN,
        INT8,UINT8,
        INT16,UINT16,
        FLOAT16,
        INT32,UINT32,
        FLOAT32,FLOAT64
    };
    struct BlockIndex{
        int x,y,z,w;
    };

    std::string getVolumeName() const {return name;}
    int getBlockLength() const {return block_length;}
    int getBlockPadding() const {return padding;}
    int getBlockLengthWithoutPadding() const {return block_length - 2 * padding;}
    VoxelType getVoxelType() const {return voxel_type;}
    void getVolumeDim(int&x,int&y,int&z) const { x = volume_dim_x; y = volume_dim_y; z = volume_dim_z;}
    void getVolumeSpace(float&x,float&y,float&z) const { x = volume_space_x; y = volume_space_y; z = volume_space_z;}
    int getBlockSize() const { return block_length * block_length * block_length;}

  public:
    std::string name{};
    int block_length{0},padding{0};
    VoxelType voxel_type{UNKNOWN};
    int volume_dim_x{0},volume_dim_y{0},volume_dim_z{0};
    float volume_space_x{0.f},volume_space_y{0.f},volume_space_z{0.f};
};



MRAYNS_END

namespace std{
    template <>
    struct hash<mrayns::Volume::BlockIndex>{
        size_t operator()(const mrayns::Volume::BlockIndex& block_index){
            return mrayns::hash(block_index.x,block_index.y,block_index.z,block_index.w);
        }
    };
}
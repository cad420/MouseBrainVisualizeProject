//
// Created by wyz on 2022/2/23.
//

#pragma once
#include <string>

/**
 * @brief Just some information about the volume, no real volume data stored.
 * Get volume data should @see IVolumeBlockProviderInterface.
 */
class Volume{
  public:
    enum VoxelType{
        INT8,UINT8,
        INT16,UINT16,
        FLOAT16,
        INT32,UINT32,
        FLOAT32,FLOAT64
    };
    struct BlockIndex{
        int x,y,z,w;
    };

    std::string getVolumeName() const;
    int getBlockLength() const;
    int getBlockPadding() const;
    int getBlockLengthWithoutPadding() const;
    VoxelType getVoxelType() const;
    void getVolumeDim(int&x,int&y,int&z) const;
    void getVolumeSpace(float&x,float&y,float&z) const;
    int getBlockSize() const;
  private:
    std::string name;
    int block_length,padding;
    VoxelType voxel_type;
    int volume_dim_x,volume_dim_y,volume_dim_z;
    float volume_space_x,volume_space_y,volume_space_z;
};
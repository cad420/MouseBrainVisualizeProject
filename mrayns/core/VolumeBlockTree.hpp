//
// Created by wyz on 2022/2/25.
//
#pragma once

#include "Volume.hpp"
#include <vector>
#include <memory>
#include "../geometry/Frustum.hpp"
MRAYNS_BEGIN

class VolumeBlockTreeImpl;
class VolumeBlockTree{
  public:
    VolumeBlockTree() ;
    ~VolumeBlockTree();
    using BlockIndex = Volume::BlockIndex;

    void buildTree(const Volume& volume);

    void clearTree();

    const Volume& getVolume() const;

    std::vector<BlockIndex> computeIntersectBlock(const Frustum& frustum,int level = 0);

    /**
     * 切片可以是特殊的BoundBox
     */
    std::vector<BlockIndex> computeIntersectBlock(const BoundBox& box,int level = 0);

  private:
    std::unique_ptr<VolumeBlockTreeImpl> impl;
};

MRAYNS_END
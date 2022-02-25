//
// Created by wyz on 2022/2/25.
//
#include "VolumeBlockTree.hpp"

MRAYNS_BEGIN

class VolumeBlockTreeImpl{
  public:
    VolumeBlockTreeImpl();
    ~VolumeBlockTreeImpl();
};
VolumeBlockTreeImpl::VolumeBlockTreeImpl()
{
}
VolumeBlockTreeImpl::~VolumeBlockTreeImpl()
{
}

VolumeBlockTree::~VolumeBlockTree()
{
    impl.reset();
}
void VolumeBlockTree::buildTree(const Volume &volume)
{
}
void VolumeBlockTree::clearTree()
{
}
const Volume &VolumeBlockTree::getVolume() const
{
    return {};
}
std::vector<Volume::BlockIndex> VolumeBlockTree::computeIntersectBlock(const Frustum &frustum)
{
    return std::vector<BlockIndex>();
}
std::vector<Volume::BlockIndex> VolumeBlockTree::computeIntersectBlock(const BoundBox &box)
{
    return std::vector<BlockIndex>();
}
VolumeBlockTree::VolumeBlockTree()
{
}

MRAYNS_END

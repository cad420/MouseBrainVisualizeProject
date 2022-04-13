//
// Created by wyz on 2022/2/25.
//
#include "VolumeBlockTree.hpp"
#include <stdexcept>
#include "../algorithm/GeometryHelper.hpp"
#include "../common/Logger.hpp"
#include <queue>
#include <unordered_set>
#include "../utils/Timer.hpp"
MRAYNS_BEGIN



/**
 * 可以采用多种Implement 比如就是最简单暴力的原始形式 或者 采用八叉树 在遍历求交速度上会更快
 */
class VolumeBlockTreeImpl{
  public:
    VolumeBlockTreeImpl();
    ~VolumeBlockTreeImpl();


    using BlockIndex = VolumeBlockTree::BlockIndex;

    void buildTree(const Volume& volume);

    void clearTree();

    const Volume& getVolume() const;

    template <typename T>
    std::vector<BlockIndex> computeIntersectBlock(T&& t,int level);

    template<typename T>
    std::vector<BlockIndex> computeIntersectBlock(T&& t, const VolumeRendererLodDist&,const Vector3f& viewPos);

  private:
    class OctTree{
    public:
        class OctNode{
        public:
            BoundBox box;
            OctNode* parent{nullptr};
            OctNode* kids[8]{nullptr};
            int level{0};
            BlockIndex index;
        };
        /**
         * @param base_len equal to no padding block length
         */
        void buildOctTree(int base_x,int base_y,int base_z,int base_len){
            int leaf_num_x = (base_x + base_len - 1) / base_len;
            int leaf_num_y = (base_y + base_len - 1) / base_len;
            int leaf_num_z = (base_z + base_len - 1) / base_len;
            int leaf_count = leaf_num_x * leaf_num_y * leaf_num_z;
            using Array3D = std::vector<std::vector<std::vector<OctNode*>>>;
            Array3D leaf_nodes;//[z][y][x]
            leaf_nodes.resize(leaf_num_z);
            for(int z = 0;z<leaf_num_z;z++){
                leaf_nodes[z].resize(leaf_num_y);
                for(int y = 0;y<leaf_num_y;y++){
                    leaf_nodes[z][y].resize(leaf_num_x);
                    for(int x= 0;x<leaf_num_x;x++){
                        auto node = new OctNode();
                        node->box = BoundBox{
                                Vector3f{static_cast<float>(x*base_len),static_cast<float>(y*base_len),static_cast<float>(z*base_len)},
                                Vector3f{static_cast<float>((x+1)*base_len),static_cast<float>((y+1)*base_len),static_cast<float>((z+1)*base_len)}
                        };
                        node->level = 0;
                        node->index = {x,y,z,0};
                        leaf_nodes[z][y][x] = node;
                    }
                }
            }
            int last_level_x = leaf_num_x;
            int last_level_y = leaf_num_y;
            int last_level_z = leaf_num_z;
            auto get_kid = [](const Array3D& nodes,int x,int y,int z,int i,int j,int k)->OctNode*{
                if(i<x && j<y && k<z){
                    return nodes[k][j][i];
                }
                else{
                    return nullptr;
                }
            };
            Array3D last_level_nodes = std::move(leaf_nodes);
            do{
                int level_x = (last_level_x + 1) / 2;
                int level_y = (last_level_y + 1) / 2;
                int level_z = (last_level_z + 1) / 2;
                Array3D level_nodes;
                level_nodes.resize(level_z);
                for(int z = 0;z<level_z;z++){
                    level_nodes[z].resize(level_y);
                    for(int y = 0;y<level_y;y++){
                        level_nodes[z][y].resize(level_x);
                        for(int x = 0;x<level_x;x++){
                            auto node = new OctNode();
                            node->kids[0] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2,y*2,z*2);
                            node->kids[1] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2+1,y*2,z*2);
                            node->kids[2] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2,y*2+1,z*2);
                            node->kids[3] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2+1,y*2+1,z*2);
                            node->kids[4] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2,y*2,z*2+1);
                            node->kids[5] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2+1,y*2,z*2+1);
                            node->kids[6] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2,y*2+1,z*2+1);
                            node->kids[7] = get_kid(last_level_nodes,last_level_x,last_level_y,last_level_z,x*2+1,y*2+1,z*2+1);
                            auto box = node->kids[0]->box;
                            for(int i = 1;i<8;i++){
                                if(node->kids[i]){
                                    box = UnionBoundBox(box,node->kids[i]->box);
                                }
                            }
                            node->box = box;
                            for(int i = 0;i<8;i++){
                                if(node->kids[i]){
                                    node->kids[i]->parent = node;
                                }
                            }
                            node->level = node->kids[0]->level + 1;
                            node->index= {x,y,z,node->level};
                            level_nodes[z][y][x] = node;
                        }
                    }
                }
                last_level_x = level_x;
                last_level_y = level_y;
                last_level_z = level_z;
                last_level_nodes = std::move(level_nodes);
            }while(last_level_x >1 || last_level_y>1 || last_level_z>1);
            assert(last_level_x== 1 && last_level_y ==1 && last_level_z==1);
            root = last_level_nodes[0][0][0];
            assert(root->parent==nullptr);
            LOG_INFO("root level: {}",root->level);
            LOG_INFO("VolumeBlockTree build finished");
        }
        void destroy(){
            std::queue<OctNode*> q;
            q.push(root);
            while(!q.empty()){
                auto p = q.front();
                q.pop();
                if(!p) continue;
                for(int i = 0;i<8;i++){
                    q.push(p->kids[i]);
                }
                delete p;
            }
        }
        ~OctTree(){
            destroy();
        }


        void traverse(std::function<void(OctNode*)> f){
            if(!root) return;
            std::queue<OctNode*> q;
            q.push(root);
            while(!q.empty()){
                auto t = q.front();
                q.pop();
                f(t);
                for(int i = 0;i < 8; i++){
                    if(t->kids[i]){
                        q.push(t->kids[i]);
                    }
                }
            }
        }

        OctNode* root;
        int levels;
    };

  private:

    void buildTree();


    Volume volume;

    OctTree oct_tree;

    int max_level{0};//not affect OctTree build but affect intersect blocks compute
};
VolumeBlockTreeImpl::VolumeBlockTreeImpl()
{
}
VolumeBlockTreeImpl::~VolumeBlockTreeImpl()
{
}
void VolumeBlockTreeImpl::buildTree(const Volume &volume)
{
    START_TIMER
    if(!volume.isValid()){
        throw std::runtime_error("pass invalid volume to build VolumeBlockTree");
    }
    this->max_level = volume.getMaxLod();
    this->volume = volume;
    this->buildTree();
    //transform to voxel space
    this->oct_tree.traverse([this](OctTree::OctNode* node){
       node->box.min_p *= this->volume.getVolumeSpace();
       node->box.max_p *= this->volume.getVolumeSpace();
    });
    STOP_TIMER("build VolumeBlockTree")
}
void VolumeBlockTreeImpl::clearTree()
{
    this->volume.clear();
    this->max_level = 0;

}
const Volume &VolumeBlockTreeImpl::getVolume() const
{
    return volume;
}

//按照普通的算法 首先计算lod0相交的块 然后根据lod-dist策略进行淘汰更换得到lod更大的块
//这样子会很慢 因为当视锥体很大时候 lod0相交的块十分多 时间可能需要十几ms的代价
//另一种策略是 从最大的lod这一层开始 如果当前层的块相交 那么求得视点与该块的最近距离所mapping的lod 对该块的子节点递归求交直到lod小于刚求的最小lod

template <typename ViewSpace>
std::vector<Volume::BlockIndex> VolumeBlockTreeImpl::computeIntersectBlock(ViewSpace&& viewSpace,const VolumeRendererLodDist &lodDist,const Vector3f& viewPos)
{
    START_TIMER
    std::vector<BlockIndex> intersect_blocks;

    std::function<int(const BoundBox&)> lodComputer = [&](const BoundBox& box)->int{
        auto block_center = (box.max_p + box.min_p) * 0.5f;
        float dist = length(block_center-viewPos);
        for(int i = 0;i<VolumeRendererLodDist::MaxLod;i++){
            if(dist < lodDist.lod_dist[i]){
                return i;
            }
        }
        return VolumeRendererLodDist::MaxLod - 1;
    };
    std::vector<OctTree::OctNode*> leaves;
    leaves.reserve(64);
    std::unordered_set<OctTree::OctNode*> res;
    std::queue<OctTree::OctNode*> q;
    q.push(oct_tree.root);
    while(!q.empty())
    {
        auto t = q.front();
        assert(t);
        q.pop();

        if(GeometryHelper::GetBoxVisibility(viewSpace,t->box) == BoxVisibility::Intersecting)
        {
            if(t->level == 0)//is leaf
            {
                leaves.push_back(t);
            }
            else
            {
                for(int i = 0; i < 8 ; i++)
                {
                    if(t->kids[i])
                    {
                        q.push(t->kids[i]);
                    }
                }
            }
        }
    }
    for(auto leaf:leaves){
        assert(leaf);
        int lod = lodComputer(leaf->box);
        while(lod-->0 && leaf->parent){
            leaf = leaf->parent;
        }
        res.insert(leaf);
    }

    intersect_blocks.reserve(res.size());
    for(auto b:res){
        intersect_blocks.emplace_back(b->index);
    }
    LOG_INFO("intersect block count {} with lod-dist",intersect_blocks.size());
    STOP_TIMER("compute intersect blocks with lod-dist");
    return intersect_blocks;
}

template <typename T>
std::vector<VolumeBlockTreeImpl::BlockIndex> VolumeBlockTreeImpl::computeIntersectBlock(T &&t, int level)
{
    std::vector<BlockIndex> intersect_blocks;

    std::queue<OctTree::OctNode*> q;
    q.push(oct_tree.root);
    while(!q.empty()){
        auto p = q.front();
        q.pop();
        if(!p) continue;
        if(GeometryHelper::GetBoxVisibility(std::forward<T>(t),p->box) == BoxVisibility::Intersecting){
            if(p->level == level){
                intersect_blocks.emplace_back(p->index);
            }
            else if(p->level > level){
                for(int i = 0;i<8;i++){
                    q.push(p->kids[i]);
                }
            }
        }
    }
//    LOG_INFO("intersect block with level ({}) count ({})",level,intersect_blocks.size());
    return intersect_blocks;
}

void VolumeBlockTreeImpl::buildTree()
{

    oct_tree.buildOctTree(volume.volume_dim_x, volume.volume_dim_y, volume.volume_dim_z,volume.getBlockLengthWithoutPadding());

}


//========== Interface ==========
VolumeBlockTree::~VolumeBlockTree()
{
    impl.reset();
}
void VolumeBlockTree::buildTree(const Volume &volume)
{
    impl->buildTree(volume);
}
void VolumeBlockTree::clearTree()
{
    impl->clearTree();
}
const Volume &VolumeBlockTree::getVolume() const
{
    return impl->getVolume();
}
std::vector<Volume::BlockIndex> VolumeBlockTree::computeIntersectBlock(const FrustumExt &frustum,int level)
{
    return impl->computeIntersectBlock(frustum,level);
}
std::vector<Volume::BlockIndex> VolumeBlockTree::computeIntersectBlock(const BoundBox &box,int level)
{
    return impl->computeIntersectBlock(box,level);
}
VolumeBlockTree::VolumeBlockTree()
{
    impl = std::make_unique<VolumeBlockTreeImpl>();
}
std::vector<Volume::BlockIndex> VolumeBlockTree::computeIntersectBlock(const FrustumExt &frustum, const VolumeRendererLodDist& lodDist,const Vector3f& viewPos)
{
    return impl->computeIntersectBlock(frustum,lodDist,viewPos);
}

MRAYNS_END

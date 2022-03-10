//
// Created by wyz on 2022/3/10.
//
#pragma once
#include "../common/MathTypes.hpp"
#include "../common/Hash.hpp"
MRAYNS_BEGIN

struct Vertex{
    Vector3f pos;
    bool operator==(const Vertex& lhs) const{
        return pos == lhs.pos;
    }
};
static_assert(sizeof(Vertex)==3*sizeof(float), "");
struct VertexExt1: public Vertex{
    Vector3f normal;
};
static_assert(sizeof(VertexExt1)==6*sizeof(float), "");
struct VertexExt2: public VertexExt1{
    Vector2f uv;

};
static_assert(sizeof(VertexExt2)==8*sizeof(float), "");


MRAYNS_END

namespace std{
template <>
struct hash<mrayns::Vertex>{
    size_t operator()(const mrayns::Vertex& lhs) const{
        return mrayns::hash(lhs.pos.x,lhs.pos.y,lhs.pos.z);
    }
};
}
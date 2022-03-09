#version 460 core

layout(location = 0) out vec4 oFragColor;

layout(input_attachment_index = 0, binding = 0) uniform subpassInput RayEntry;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput RayExit;

layout(binding = 2) uniform sampler1D TransferTable;

const int MaxTextureNum = 12;

layout(binding = 3) uniform sampler3D CachedVolume[MaxTextureNum];

void main() {

}

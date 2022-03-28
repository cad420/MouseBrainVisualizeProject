#version 460 core

layout(location = 0) out vec4 oFragColor;

layout(input_attachment_index = 0, binding = 0) uniform subpassInput RayEntry;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput RayExit;

layout(binding = 2) uniform sampler1D TransferTable;

const int MaxTextureNum = 16;
const int MaxVolumeLod = 12;
layout(binding = 3) uniform sampler3D CachedVolume[MaxTextureNum];

//uoload once for one volume
layout(binding = 4) uniform VolumeInfoUBO{
    ivec4 volume_dim;//x y z and max_lod
    ivec3 lod0_block_dim;
    int block_length;
    int padding;
    vec3 volume_space;
    vec3 inv_volume_space;
    float voxel;
    uint lod_page_table_offset[MaxVolumeLod];
    vec3 inv_texture_shape;
}volumeInfoUBO;

//layout(std140,binding = 5) readonly buffer PageTable{
//    ivec4 entry_value[];
//}pageTable;
const int HashTableSize = 1024;
layout(binding = 5) uniform PageTable{
    ivec4 hash_table[HashTableSize];
}pageTable;

layout(binding = 6) uniform RenderParams{
    float lod_dist[MaxVolumeLod];
    float ray_step;
    vec3 view_pos;
}renderParams;

void main() {
    vec3 ray_entry_pos = subpassLoad(RayEntry).xyz;
}

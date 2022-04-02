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
    uvec4 volume_dim;//x y z and max_lod
    uvec3 lod0_block_dim;
    uint block_length;
    uint padding;
    vec3 volume_space;
    vec3 inv_volume_space;
    vec3 block_length_space;
    float voxel;
    uint lod_page_table_offset[MaxVolumeLod];
    vec3 inv_texture_shape;
}volumeInfoUBO;

//64mb uniform buffer
const int HashTableSize = 1024;
layout(binding = 5) uniform PageTable{
    uvec4 hash_table[HashTableSize][2];
}pageTable;

uint GetHashValue(in uvec4 key){
    uint value = key[0];
    for(int i = 1; i < 4; i++){
        value = value ^  (key[i] + 0x9e3779b9 + (value << 6) + (value >> 2));
    }
    return value;
}
uvec4 QueryPageTable(in uvec4 key){
    uint hash_v = GetHashValue(key);
    uint pos = hash_v % HashTableSize;
    int i = 0;
    bool positive = false;
    while(true){
        int ii = i*i;
        pos += positive ? ii : -ii;
        pos %= HashTableSize;
        if(GetHashValue(pageTable.hash_table[pos][0]) == hash_v){
            return pageTable.hash_table[pos][1];
        }
        if(!positive) i++;
        positive = !positive;
        if(i > HashTableSize){
            break;
        }
    }
    return uvec4(MaxTextureNum);
}

layout(binding = 6) uniform RenderParams{
    float lod_dist[MaxVolumeLod];
    float ray_dist;
    float ray_step;
    vec3 view_pos;//used for lod dist compute
}renderParams;

//计算视点到当前块中心的距离 块一直使用lod0
float ComputeDistanceFromViewPosToBlockCenter(in vec3 viewPos){
    vec3 block_index = vec3(uvec3(viewPos / volumeInfoUBO.block_length_space));
    vec3 block_center = (block_index+vec3(0.5)) * volumeInfoUBO.block_length_space;
    return length(block_center - renderParams.viewPos);
}

float VirtualSample(){



    return 0.f;
}

void main() {
    vec3 ray_entry_pos = subpassLoad(RayEntry).xyz;
    vec3 ray_exit_pos = subpassLoad(RayExit).xyz;
    vec3 ray_entry_to_exit = ray_exit_pos - ray_entry_pos;
    vec3 ray_direction = normalize(ray_entry_to_exit);
    float ray_max_cast_dist = min(renderParams.ray_dist,dot(ray_entry_to_exit,ray_direction));
    int ray_max_cast_steps = int(ray_max_cast_dist / renderParams.ray_step);


    oFragColor = vec4(ray_direction,1.f);
}

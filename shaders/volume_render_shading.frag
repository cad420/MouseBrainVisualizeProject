#version 460 core

layout(location = 0) out vec4 oFragColor;

layout(input_attachment_index = 0, binding = 0) uniform subpassInput RayEntry;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput RayExit;

layout(binding = 2) uniform sampler1D TransferTable;

const int MaxTextureNum = 16;
const int MaxVolumeLod = 12;
const int MaxGPUTextureCount = 16;
layout(binding = 3) uniform sampler3D CachedVolume[MaxTextureNum];

//uoload once for one volume
layout(std140,binding = 4) uniform VolumeInfoUBO{
    uvec4 volume_dim;//x y z and max_lod //16
    uvec3 lod0_block_dim; //16
    vec3 volume_space; //16
    vec3 inv_volume_space; //16
    vec3 virtual_block_length_space; //16
    uint virtual_block_length; //4
    uint padding; //4
    uint padding_block_length; //4
    float voxel; //4

    vec3 inv_texture_shape[MaxGPUTextureCount];//256
}volumeInfoUBO;

//64mb uniform buffer
const int HashTableSize = 1024;
layout(binding = 5) uniform PageTable{
    ivec4 hash_table[HashTableSize][2];
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
        if(pageTable.hash_table[pos][0].w >=0 && GetHashValue(pageTable.hash_table[pos][0]) == hash_v){
            return pageTable.hash_table[pos][1];
        }
        else if(pageTable.hash_table[pos][0].w < 0){
            return uvec4(MaxTextureNum);
        }
        if(!positive) i++;
        positive = !positive;
        if(i > HashTableSize){
            break;
        }
    }
    return uvec4(MaxTextureNum);
}

layout(std140,binding = 6) uniform RenderParams{
    vec3 view_pos;//used for lod dist compute
    float ray_dist;
    float ray_step;
    float lod_dist[MaxVolumeLod];
}renderParams;

//计算视点到当前块中心的距离 块一直使用lod0
float ComputeDistanceFromViewPosToBlockCenter(in vec3 rayPos){
    vec3 block_index = vec3(uvec3(rayPos / volumeInfoUBO.virtual_block_length_space));
    vec3 block_center = (block_index+vec3(0.5)) * volumeInfoUBO.virtual_block_length_space;
    return length(block_center - renderParams.view_pos);
}
//must return valid lod
int ComputeCurrentSampleLod(in vec3 rayPos){
    float dist = ComputeDistanceFromViewPosToBlockCenter(rayPos);
    for(int lod = 0;lod < MaxVolumeLod; lod++){
        if(dist < renderParams.lod_dist[lod]){
            return lod;
        }
    }

    return MaxVolumeLod - 1;
}

// no check for samplePos, caller should check if samplePos is valid
int VirtualSample(in int sampleLod,in vec3 samplePos,out float sampleScalar){
    int sampleLodT = 1 << sampleLod;
    vec3 block_index = vec3(ivec3(samplePos / (volumeInfoUBO.virtual_block_length_space * sampleLodT)));

    uvec4 texture_entry = QueryPageTable(uvec4(block_index,sampleLod));
    if(texture_entry.w == MaxTextureNum){
        return 0;
    }
    //no check for texture entry
    vec3 offset_in_virtual_block = (samplePos * volumeInfoUBO.inv_volume_space - block_index * volumeInfoUBO.virtual_block_length * sampleLodT) / sampleLodT;
    vec3 texture_sample_coord = (texture_entry.xyz * volumeInfoUBO.padding_block_length + offset_in_virtual_block + vec3(volumeInfoUBO.padding)) * volumeInfoUBO.inv_texture_shape[texture_entry.w];



    sampleScalar = texture(CachedVolume[texture_entry.w],texture_sample_coord).r;

    if(false){
        oFragColor = vec4(texture_sample_coord,sampleScalar);
        return 2;
    }

    return 1;
}

void main() {
    vec3 ray_entry_pos = subpassLoad(RayEntry).xyz;
    vec3 ray_exit_pos = subpassLoad(RayExit).xyz;
    vec3 ray_entry_to_exit = ray_exit_pos - ray_entry_pos;
    vec3 ray_direction = normalize(ray_entry_to_exit);
    float ray_max_cast_dist = min(renderParams.ray_dist,dot(ray_entry_to_exit,ray_direction));
    int ray_max_cast_steps = int(ray_max_cast_dist / renderParams.ray_step);

    vec4 accumelate_color = vec4(0.f);

    vec3 ray_cast_pos = ray_entry_pos;//not camera pos if outside volume
    int last_sample_lod = 0;
    vec3 last_lod_sample_pos = ray_entry_pos;
    int last_lod_sample_steps = 0;



    for(int i = 0; i < ray_max_cast_steps; i++){
        float ray_cast_dist = dot(ray_cast_pos-ray_entry_pos,ray_direction);
        if(ray_cast_dist > ray_max_cast_dist){
            break;
        }
        int cur_sample_lod = ComputeCurrentSampleLod(ray_cast_pos);

        if(cur_sample_lod > last_sample_lod){
            last_lod_sample_pos = ray_cast_pos;
            last_lod_sample_steps = i;
            last_sample_lod = cur_sample_lod;
        }
        float cur_sample_step = (1 << cur_sample_lod) * renderParams.ray_step;
        float sample_scalar = 0.f;
        //可以对没有加载的块进行整块跳过
        int ret = VirtualSample(cur_sample_lod,ray_cast_pos,sample_scalar);

        if(ret == 0){ // 该块没找到 可以进行跳过
//            accumelate_color = vec4(1.f,1.f,1.f,1.f);
//            break;
        }
        else if(ret == 2){

            return;
        }

        if(sample_scalar > 0.f){
            vec4 sample_color = texture(TransferTable,sample_scalar);
            if(sample_color.w > 0.f){
                accumelate_color += sample_color * vec4(sample_color.aaa,1.f) * (1.f - accumelate_color.a);
                if(accumelate_color.a > 0.99f){
                    break;
                }
            }
        }

        ray_cast_pos = last_lod_sample_pos + (i + 1 - last_lod_sample_steps) * ray_direction * cur_sample_step;

    }
//    oFragColor = vec4(ray_direction,1.f);
//    return;
    if(accumelate_color.a == 0.f) discard;
    oFragColor = accumelate_color;
}

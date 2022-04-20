#version 460 core

layout(binding = 0,rgba32f) uniform image2D RayEntry;
layout(binding = 1,rgba32f) uniform image2D RayExit;
layout(binding = 2,rgba8) uniform image2D InterColor;
layout(std430,binding = 3) buffer RenderPassTag{
    uint finished;
}renderPassTag;
layout(binding = 4) uniform sampler1D TransferTable;

const int MaxTextureNum = 16;
const int MaxVolumeLod = 12;
const int MaxGPUTextureCount = 16;
layout(binding = 5) uniform sampler3D CachedVolume[MaxTextureNum];

layout(std140,binding = 6) uniform VolumeInfoUBO{
    vec4 volume_board;//x y z and max_lod //16
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
layout(binding = 7) uniform PageTable{
    ivec4 hash_table[HashTableSize][2];
}pageTable;

uint GetHashValue(in uvec4 key){
    uint value = key[0];
    for(int i = 1; i < 4; i++){
        value = value ^  (key[i] + 0x9e3779b9 + (value << 6) + (value >> 2));
    }
    return value;
}
#define MAX_QUERY_NUM 64
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
        if(i > MAX_QUERY_NUM){
            break;
        }
    }
    return uvec4(MaxTextureNum);
}

layout(std140,binding = 8) uniform RenderParams{
    vec3 view_pos;//used for lod dist compute
    float ray_dist;//start form view_pos, not equal to real ray cast distance
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
bool InsideVolume(in vec3 samplePos){
    return samplePos.x >= 0.f && samplePos.y >= 0.f && samplePos.z >= 0.f
    && samplePos.x <= volumeInfoUBO.volume_board.x
    && samplePos.y <= volumeInfoUBO.volume_board.y
    && samplePos.z <= volumeInfoUBO.volume_board.z;
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

    return 1;
}
void VirtualSampleExt(in int sampleLod,in vec3 samplePos,vec3 offset,out float sampleScalar){
    int sampleLodT = 1 << sampleLod;
    vec3 block_index = vec3(ivec3(samplePos / (volumeInfoUBO.virtual_block_length_space * sampleLodT)));

    uvec4 texture_entry = QueryPageTable(uvec4(block_index,sampleLod));
    if(texture_entry.w == MaxTextureNum){
        return;
    }
    //no check for texture entry
    vec3 offset_in_virtual_block = ((samplePos + offset) * volumeInfoUBO.inv_volume_space - block_index * volumeInfoUBO.virtual_block_length * sampleLodT) / sampleLodT;
    vec3 texture_sample_coord = (texture_entry.xyz * volumeInfoUBO.padding_block_length + offset_in_virtual_block + vec3(volumeInfoUBO.padding)) * volumeInfoUBO.inv_texture_shape[texture_entry.w];

    sampleScalar = texture(CachedVolume[texture_entry.w],texture_sample_coord).r;

}
vec2 IntersectWithAABB(in vec3 minP,in vec3 maxP,in vec3 rayPos,in vec3 invRayDirection){

    float t_min_x = (minP.x - rayPos.x) * invRayDirection.x;
    float t_max_x = (maxP.x - rayPos.x) * invRayDirection.x;
    if(invRayDirection.x < 0.f){
        float t = t_min_x;
        t_min_x = t_max_x;
        t_max_x = t;
    }

    float t_min_y = (minP.y - rayPos.y) * invRayDirection.y;
    float t_max_y = (maxP.y - rayPos.y) * invRayDirection.y;
    if(invRayDirection.y < 0.f){
        float t = t_min_y;
        t_min_y = t_max_y;
        t_max_y = t;
    }

    float t_min_z = (minP.z - rayPos.z) * invRayDirection.z;
    float t_max_z = (maxP.z - rayPos.z) * invRayDirection.z;
    if(invRayDirection.z < 0.f){
        float t = t_min_z;
        t_min_z = t_max_z;
        t_max_z = t;
    }

    float enter_t = max(t_min_x,max(t_min_y,t_min_z));
    float exit_t  = min(t_max_x,min(t_max_y,t_max_z));
    return vec2(enter_t,exit_t);
}
vec3 GetRayExitPos(in vec3 rayDirection,in vec3 rayPos,in int sampleLod){
    int sampleLodT = 1 << sampleLod;
    vec3 block_index = vec3(ivec3(rayPos / (volumeInfoUBO.virtual_block_length_space * sampleLodT)));
    vec3 box_min_pos = block_index * volumeInfoUBO.virtual_block_length_space;
    vec3 box_max_pos = (block_index + vec3(1.f)) * volumeInfoUBO.virtual_block_length_space * sampleLodT;
    vec2 t = IntersectWithAABB(box_min_pos,box_max_pos,rayPos,1.f/rayDirection);
    vec3 skip_pos = rayPos + rayDirection * t.y;
    return skip_pos;
}
vec3 PhongShading(in vec3 diffuseColor,in int sampleLod,in vec3 samplePos,in vec3 viewDirection){
    vec3 N;
    float x1,x2;
    int sampleLodT = 1 << sampleLod;
    float voxel = volumeInfoUBO.voxel * sampleLodT;
    int ret;
    float scalar;
    int miss = 0;
    VirtualSample(sampleLod,samplePos,scalar);
    VirtualSampleExt(sampleLod,samplePos,vec3(voxel,0.f,0.f),x1);
//    if(ret == 0){
//        x1 = scalar;
//        miss += 1;
//    }
    VirtualSampleExt(sampleLod,samplePos,vec3(-voxel,0.f,0.f),x2);
//    if(ret == 0){
//        x2 = scalar;
//        miss += 1;
//    }
    N.x = x1 - x2;
    VirtualSampleExt(sampleLod,samplePos,vec3(0.f,voxel,0.f),x1);
//    if(ret == 0){
//        x1 = scalar;
//        miss += 1;
//    }
    VirtualSampleExt(sampleLod,samplePos,vec3(0.f,-voxel,0.f),x2);
//    if(ret == 0){
//        x2 = scalar;
//        miss += 1;
//    }
    N.y = x1 - x2;
    VirtualSampleExt(sampleLod,samplePos,vec3(0.f,0.f,voxel),x1);
//    if(ret == 0){
//        x1 = scalar;
//        miss += 1;
//    }
    VirtualSampleExt(sampleLod,samplePos,vec3(0.f,0.f,-voxel),x2);
//    if(ret == 0){
//        x2 = scalar;
//        miss += 1;
//    }
    N.z = x1 - x2;
    if(miss == 6 ){
        N = - viewDirection;
    }
    else{
        N = -normalize(N);
    }
    vec3 ambient = 0.05f * diffuseColor;
    vec3 diffuse = max(dot(N,-viewDirection),0.f) * diffuseColor;
    return ambient + diffuse;
}
void main() {
    vec4 accumelate_color = imageLoad(InterColor,ivec2(gl_FragCoord)).rgba;
    if(accumelate_color.a > 0.99f){
        discard;
    }
    vec3 ray_entry_pos = imageLoad(RayEntry,ivec2(gl_FragCoord)).xyz;


    vec3 ray_exit_pos = imageLoad(RayExit,ivec2(gl_FragCoord)).xyz;
    vec3 ray_entry_to_exit = ray_exit_pos - ray_entry_pos;
    vec3 ray_direction = normalize(ray_entry_to_exit);
    //re-compute ray_entry_pos
    if(!InsideVolume(ray_entry_pos)){
        vec2 t = IntersectWithAABB(vec3(0.f),volumeInfoUBO.volume_board.xyz,ray_entry_pos,1.f/ray_direction);
        if(t.x >= 0.f){
            ray_entry_pos += ray_direction * t.x * 1.01f;
            ray_entry_to_exit = ray_exit_pos - ray_entry_pos;
        }
    }
    float ray_max_cast_dist_from_entry = dot(ray_entry_to_exit,ray_direction);
    int ray_max_cast_steps_from_entry = int(ray_max_cast_dist_from_entry / renderParams.ray_step);
    float ray_max_cast_dist_from_view = min(renderParams.ray_dist,dot(ray_direction,ray_exit_pos-renderParams.view_pos));
    vec3 ray_cast_pos = ray_entry_pos;//not camera pos if outside volume
    int last_sample_lod = ComputeCurrentSampleLod(ray_cast_pos);
    vec3 last_lod_sample_pos = ray_entry_pos;
    int last_lod_sample_steps = 0;



    for(int i = 0; i < ray_max_cast_steps_from_entry; i++){
        //ray cast distance compute by view pos not ray entry pos
        float ray_cast_dist_from_view = dot(ray_cast_pos-renderParams.view_pos,ray_direction);
        if(ray_cast_dist_from_view > ray_max_cast_dist_from_view){
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

        int ret = VirtualSample(cur_sample_lod,ray_cast_pos,sample_scalar);

        if(ret == 0){ // 该块没找到 终止光线投射 记录当前位置和颜色
            //检查是否因为浮点误差而到了体外

            if(!InsideVolume(ray_cast_pos)){
                break;
            }
            else{
                imageStore(RayEntry,ivec2(gl_FragCoord),vec4(ray_cast_pos,cur_sample_lod));
                imageStore(InterColor,ivec2(gl_FragCoord),accumelate_color);
                if(renderPassTag.finished == 0)
                    atomicExchange(renderPassTag.finished,1);
                discard;
            }
        }

        if(sample_scalar > 0.f){
            vec4 sample_color = texture(TransferTable,sample_scalar);
            if(sample_color.w > 0.f){
                sample_color.rgb = PhongShading(sample_color.rgb,cur_sample_lod,ray_cast_pos,ray_direction);
                accumelate_color += sample_color * vec4(sample_color.aaa,1.f) * (1.f - accumelate_color.a);
                if(accumelate_color.a > 0.99f){
                    break;
                }
            }
        }

        ray_cast_pos = last_lod_sample_pos + (i + 1 - last_lod_sample_steps) * ray_direction * cur_sample_step;

    }
    if(accumelate_color.a == 0.f) discard;//?
    accumelate_color.a = 1.f;
    accumelate_color.rgb = pow(accumelate_color.rgb,vec3(1.0/2.2));
    imageStore(InterColor,ivec2(gl_FragCoord),accumelate_color);
    discard;
}

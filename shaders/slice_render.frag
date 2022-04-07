#version 460 core

layout(location = 0) out vec4 oFragColor;

const int MaxTextureNum = 16;
const int MaxVolumeLod = 12;
layout(binding = 0) uniform sampler3D CachedVolume[MaxTextureNum];
layout(binding = 1) uniform sampler1D TransferTable;
layout(std140,binding = 2) uniform VolumeInfoUBO{
    uvec4 volume_dim;//x y z and max_lod //16
    uvec3 lod0_block_dim; //16
    vec3 volume_space; //16
    vec3 inv_volume_space; //16
    vec3 virtual_block_length_space; //16
    uint virtual_block_length; //4
    uint padding; //4
    uint padding_block_length; //4
    float voxel; //4

    vec3 inv_texture_shape[MaxTextureNum];//256
}volumeInfoUBO;

//64mb uniform buffer
const int HashTableSize = 1024;
layout(binding = 3) uniform PageTable{
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
const int RENDER_TYPE_MIP = 0;
const int RENDER_TYPE_RAYCAST = 1;
layout(std140,binding = 4) uniform RenderParams{
    vec3 origin;
    vec3 normal;
    vec3 x_dir;//normalized
    vec3 y_dir;//normalized
    vec2 min_p;
    vec2 max_p;
    ivec2 window;
    float voxels_per_pixel;
    int lod;
    float step; // 0.5 * voxel
    float depth;//depends on lod
    uint render_type;
}renderParams;
bool InRenderRegion(vec2 uv){
    bool ok1 = uv.x > renderParams.min_p.x && uv.y > renderParams.min_p.y;
    bool ok2 = uv.x < renderParams.max_p.x + 1.0 && uv.y < renderParams.max_p.y + 1.0;
    return ok1 && ok2;
}
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
vec4 MipRender(vec2 uv){
    vec3 ray_start_pos = renderParams.origin
                       + uv.x * renderParams.voxels_per_pixel * volumeInfoUBO.voxel * renderParams.x_dir
                       + uv.y * renderParams.voxels_per_pixel * volumeInfoUBO.voxel * renderParams.y_dir;
    ray_start_pos = ray_start_pos + 0.5 * renderParams.depth * renderParams.normal;
    int steps = int(renderParams.depth / renderParams.step) + 1;
    float c_step = renderParams.depth / steps;
    float max_scalar = 0.f;
    vec3 ray_sample_pos = ray_start_pos;
    vec3 ray_direction = -renderParams.normal;
    int sample_lod = renderParams.lod;
    for(int i = 0 ; i < steps; i++){
        float sample_scalar;

        int ret = VirtualSample(sample_lod,ray_sample_pos,sample_scalar);

        if(sample_scalar > max_scalar){
            max_scalar = sample_scalar;
        }
        if(max_scalar == 1.f){
            break;
        }
        ray_sample_pos = ray_start_pos + (i + 1) * c_step * ray_direction;
    }

    return vec4(max_scalar,max_scalar,max_scalar,1.f);
}

//只适用于深度较小的raycast 对于depth很大的情况 会很慢 因为没有lod变化

vec4 RayCastRender(vec2 uv){
    vec3 ray_start_pos = renderParams.origin
    + uv.x * renderParams.voxels_per_pixel * volumeInfoUBO.voxel * renderParams.x_dir
    + uv.y * renderParams.voxels_per_pixel * volumeInfoUBO.voxel * renderParams.y_dir;
    ray_start_pos = ray_start_pos + 0.5 * renderParams.depth * renderParams.normal;
    int steps = int(renderParams.depth / renderParams.step) + 1;
    float c_step = renderParams.depth / steps;
    vec3 ray_sample_pos = ray_start_pos;
    vec3 ray_direction = -renderParams.normal;
    int sample_lod = renderParams.lod;
    vec4 accumelate_color = vec4(0.f);
    for(int i = 0 ; i < steps; i++){
        float sample_scalar;

        int ret = VirtualSample(sample_lod,ray_sample_pos,sample_scalar);

        if(sample_scalar > 0.f){
            vec4 sample_color = texture(TransferTable,sample_scalar);
            if(sample_color.w > 0.f){
                accumelate_color += sample_color * vec4(sample_color.aaa,1.f) * (1.f - accumelate_color.a);
                if(accumelate_color.a > 0.99f){
                    break;
                }
            }
        }

        ray_sample_pos = ray_start_pos + (i + 1) * c_step * ray_direction;
    }

    return accumelate_color;
}

void main() {
    vec2 uv = vec2(gl_FragCoord);
    if(!InRenderRegion(uv)){
        discard;
    }
    uv.y = renderParams.window.y - uv.y;

    if(renderParams.render_type == RENDER_TYPE_MIP)
        oFragColor = MipRender(uv);
    else if(renderParams.render_type == RENDER_TYPE_RAYCAST)
        oFragColor = RayCastRender(uv);
    else
        oFragColor = vec4(1.f);

    if(oFragColor.a == 0.f)
        discard;

}

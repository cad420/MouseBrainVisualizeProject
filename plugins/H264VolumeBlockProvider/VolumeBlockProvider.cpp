//
// Created by wyz on 2022/2/24.
//
#include "VolumeBlockProvider.hpp"

MRAYNS_BEGIN
void H264VolumeBlockProvider::open(const std::string &filename)
{
    if(filename.empty()) return;
    LodFile lod_file;
    lod_file.open_lod_file(filename);
    for(int i =lod_file.get_min_lod();i<=lod_file.get_max_lod();i++){
        lod_reader[i] = std::make_unique<Reader>(lod_file.get_lod_file_path(i).c_str());
        lod_reader[i]->read_header();
    }
    sv::Header header;
    lod_reader[lod_file.get_min_lod()]->read_header(header);
    volume.padding = header.padding;
    volume.block_length = std::pow(2,header.log_block_length);
    volume.name = "mouse";
    volume.volume_dim_x = header.raw_x;
    volume.volume_dim_y = header.raw_y;
    volume.volume_dim_z = header.raw_z;
    auto space = lod_file.get_volume_space();
    volume.volume_space_x = space[0];
    volume.volume_space_y = space[1];
    volume.volume_space_z = space[2];
    volume.voxel_type = Volume::UINT8;
}
void H264VolumeBlockProvider::setHostNode(HostNode* hostNode)
{
    this->host_node = hostNode;

    VoxelUncompressOptions opts;
    opts.device_id = 0;
    opts.cu_ctx = nullptr;
    opts.use_device_frame_buffer = true;
    this->worker = std::make_unique<VoxelUncompress>(opts);
}
const Volume &mrayns::H264VolumeBlockProvider::getVolume() const
{
    return this->volume;
}
void H264VolumeBlockProvider::getVolumeBlock(void *dst,BlockIndex blockIndex)
{
    auto gpu_tasks = host_node->getAllGPUTasks();
    std::vector<std::vector<uint8_t>> packets;
    lod_reader[blockIndex.w]->read_packet(
        {(uint32_t)blockIndex.x,(uint32_t)blockIndex.y,(uint32_t)blockIndex.z},packets);
    worker->uncompress(reinterpret_cast<uint8_t*>(dst),getVolume().getBlockSize(),packets);

}

MRAYNS_END

REGISTER_PLUGIN_FACTORY_IMPL(H264VolumeBlockProviderFactory)
EXPORT_PLUGIN_FACTORY_IMPL(H264VolumeBlockProviderFactory)


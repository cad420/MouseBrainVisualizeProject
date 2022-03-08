//
// Created by wyz on 2022/2/24.
//
#include "core/BlockVolumeManager.hpp"
#include "plugin/PluginLoader.hpp"
#include <iostream>
#include <fstream>
using namespace mrayns;
int main(){
    PluginLoader::LoadPlugins("./plugins");
    auto p = std::unique_ptr<IVolumeBlockProviderInterface>(
        PluginLoader::CreatePlugin<IVolumeBlockProviderInterface>("block-provider"));
    std::string h264_file_path="E:/MouseNeuronData/mouse_file_config.json";
    p->open(h264_file_path);

    auto& host_node = HostNode::getInstance();
    host_node.setGPUNum(1);

    p->setHostNode(&host_node);

    auto volume = p->getVolume();

    BlockVolumeManager& block_volume_manager = BlockVolumeManager::getInstance();

    block_volume_manager.setProvider(std::move(p));

    int dim_x,dim_y,dim_z;
    block_volume_manager.getVolume().getVolumeDim(dim_x,dim_y,dim_z);
    std::cout<<"volume dim "<<dim_x<<" "<<dim_y<<" "<<dim_z<<std::endl;
    std::cout<<"volume block length "<<block_volume_manager.getVolume().getBlockLength()<<std::endl;
    auto data = block_volume_manager.getVolumeBlock({1,2,0,4});
    auto size = block_volume_manager.getVolume().getBlockSize();

    auto save_data_to_file = [](void* data,size_t size,const std::string& name){
        if(!data || size ==0){
            std::cout<<"empty buffer to save"<<std::endl;
        }
        std::ofstream out(name,std::ios::binary);
        if(!out.is_open()){
            std::cout<<"open save file failed"<<std::endl;
            return;
        }
        out.write(reinterpret_cast<char*>(data),size);
        out.close();
    };
    save_data_to_file(data,size,"gen#1#2#0#4_512_512_512_uint8.raw");
}
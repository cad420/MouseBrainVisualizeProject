//
// Created by wyz on 2022/2/24.
//
#pragma once

#include "plugin/Plugin.hpp"
#include "extension/VolumeBlockProviderInterface.hpp"
#include <unordered_map>
#include "VoxelCompression/voxel_uncompress/VoxelUncompress.h"
#include "VoxelCompression/voxel_compress/VoxelCmpDS.h"
using namespace sv;
MRAYNS_BEGIN

class H264VolumeBlockProvider: public IVolumeBlockProviderInterface{
  public:
    H264VolumeBlockProvider();
    void open(const std::string& filename) override;

    void setHostNode(HostNode* hostNode) override;

    const Volume& getVolume() const override;

    void getVolumeBlock(void* dst,BlockIndex blockIndex) override;

  private:

    struct Impl;
    std::unique_ptr<Impl> impl;

    HostNode* host_node;

    Volume volume;
};

MRAYNS_END

class H264VolumeBlockProviderFactory: public mrayns::IPluginFactory{
  public:
    std::string Key() const override{
        return "block-provider";
    }
    void *Create(const std::string &key) override{
        return new ::mrayns::H264VolumeBlockProvider();
    }
    std::string GetModuleID() const override{
        return ::mrayns::module_id_traits<::mrayns::IVolumeBlockProviderInterface>::GetModuleID();
    }
};

REGISTER_PLUGIN_FACTORY_DECL(H264VolumeBlockProviderFactory)
EXPORT_PLUGIN_FACTORY_DECL(H264VolumeBlockProviderFactory)
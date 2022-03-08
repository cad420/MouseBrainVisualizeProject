//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "../core/HostNode.hpp"
#include "../core/Volume.hpp"
#include "plugin/PluginDefine.hpp"
#include <functional>
MRAYNS_BEGIN
/**
 * @brief
 */
class IVolumeBlockProviderInterface{
  public:
    using BlockIndex = Volume::BlockIndex;

    virtual ~IVolumeBlockProviderInterface() = default;

    virtual void open(const std::string& filename) = 0;

    //todo: test if instance between two dll is same
    virtual void setHostNode(HostNode* hostNode) = 0;

    virtual const Volume& getVolume() const = 0;

    /**
     * @brief This is a sycn function that will return until decoding finish
     * @param dst caller should pass valid memory ptr to receive data.
     *
     */
    virtual void getVolumeBlock(void* dst,BlockIndex blockIndex) = 0;

    virtual void getVolumeBlock(void* dst,BlockIndex blockIndex,std::function<int(HostNode*)> gpu_selector){}

};

DECLARE_PLUGIN_MODULE_ID(IVolumeBlockProviderInterface,"mrayns.core.block-provider")

MRAYNS_END


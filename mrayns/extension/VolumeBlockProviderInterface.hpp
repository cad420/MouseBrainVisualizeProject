//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "../core/Volume.hpp"

/**
 * @brief
 */
class IVolumeBlockProviderInterface{
  public:
    using BlockIndex = Volume::BlockIndex;

    virtual ~IVolumeBlockProviderInterface() = default;

    virtual void open(const std::string& filename) = 0;

    virtual const Volume& getVolume() const = 0;

    /**
     * @param dst caller should pass valid memory ptr to receive data.
     */
    virtual void getVolumeBlock(void* dst,BlockIndex blockIndex) = 0;

};
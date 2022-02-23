//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "Volume.hpp"
#include "../extension/VolumeBlockProviderInterface.hpp"
#include <memory>
/**
 *
 */
class BlockVolumeManager{
  public:
    using BlockIndex = Volume::BlockIndex;

    static BlockVolumeManager& getInstance();

    void init(std::unique_ptr<IVolumeBlockProviderInterface>&& provider);

    void clear();

    bool isValid() const;

    const Volume& getVolume() const;

    void* getVolumeBlock(BlockIndex blockIndex,bool sync = true);

    void* getVolumeBlockAndLock(BlockIndex blockIndex,bool sync = true);

    bool lock(void* ptr);

    void waitForLock(void* ptr);

    bool unlock(void* ptr);

    void waitForUnlock(void* ptr);

  private:
    BlockVolumeManager();
};
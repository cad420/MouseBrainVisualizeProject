//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "Volume.hpp"
#include "../extension/VolumeBlockProviderInterface.hpp"
#include <memory>
MRAYNS_BEGIN
/**
 *
 */
class BlockVolumeManager{
  public:
    using BlockIndex = Volume::BlockIndex;

    /**
     * Single instance for a program
     */
    static BlockVolumeManager& getInstance();

    void init(std::unique_ptr<IVolumeBlockProviderInterface>&& provider);

    void clear();

    bool isValid() const;

    const Volume& getVolume() const;

    void* getVolumeBlock(const BlockIndex& blockIndex,bool sync = true);

    void* getVolumeBlockAndLock(const BlockIndex& blockIndex,bool sync = true);

    bool lock(void* ptr);

    void waitForLock(void* ptr);

    //lock is not always successful because may locked by internal waiting for data from provider
    bool unlock(void* ptr);

    void waitForUnlock(void* ptr);

  private:
    BlockVolumeManager();

    std::vector<uint8_t> storage;
    std::unique_ptr<IVolumeBlockProviderInterface> provider;
};
MRAYNS_END
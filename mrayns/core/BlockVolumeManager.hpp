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

    BlockVolumeManager(const BlockVolumeManager&) = delete;
    BlockVolumeManager& operator=(const BlockVolumeManager&) = delete;
    void setProvider(std::unique_ptr<IVolumeBlockProviderInterface>&& provider);

    /**
     * @brief This should call after setProvider otherwise will rise exception.
     * It will malloc memory for storage.
     *
     */
    void init();

    void clear();

    void destroy();

    bool isValid() const;

    const Volume& getVolume() const;

    /**
     * @return if store the block buffer return true otherwise false
     */
    bool query(const BlockIndex& blockIndex);

    /**
     * @brief get volume block ptr
     * This method is not suit for multi-thread
     * @sync meanings wait for decode result, so it also may wait for some internal locks
     * @return nullptr represent not find the volume block or no empty memory to load it
     */
    void* getVolumeBlock(const BlockIndex& blockIndex,bool sync = true);

    /**
     *
     */
    void* getVolumeBlockAndLock(const BlockIndex& blockIndex);

    /**
     * @brief lock the ptr meanings the ptr in the internal will not modified
     */
    bool lock(void* ptr);

    void waitForLock(void* ptr);

    //lock is not always successful because may locked by internal waiting for data from provider
    bool unlock(void* ptr);

    void waitForUnlock(void* ptr);


  private:
    BlockVolumeManager();

    struct BlockVolumeManagerImpl;
    std::unique_ptr<BlockVolumeManagerImpl> impl;
    std::unique_ptr<IVolumeBlockProviderInterface> provider;
    Volume volume;

};
MRAYNS_END
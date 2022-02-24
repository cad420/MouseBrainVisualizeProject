//
// Created by wyz on 2022/2/24.
//
#include "BlockVolumeManager.hpp"
#include <cassert>
MRAYNS_BEGIN
BlockVolumeManager &mrayns::BlockVolumeManager::getInstance()
{
    static BlockVolumeManager block_volume_manager;
    return block_volume_manager;
}
void BlockVolumeManager::init(std::unique_ptr<IVolumeBlockProviderInterface> &&provider)
{
    this->provider = std::move(provider);

    storage.resize(this->provider->getVolume().getBlockSize());
}
void BlockVolumeManager::clear()
{
}
bool BlockVolumeManager::isValid() const
{
    return false;
}
const Volume &BlockVolumeManager::getVolume() const
{
    return this->provider->getVolume();
}
void *BlockVolumeManager::getVolumeBlock(BlockVolumeManager::BlockIndex blockIndex, bool sync)
{
    assert(provider.get());

    provider->getVolumeBlock(storage.data(),blockIndex);
    return storage.data();
}
void *BlockVolumeManager::getVolumeBlockAndLock(BlockVolumeManager::BlockIndex blockIndex, bool sync)
{
    return nullptr;
}
bool BlockVolumeManager::lock(void *ptr)
{
    return false;
}
void BlockVolumeManager::waitForLock(void *ptr)
{
}
bool BlockVolumeManager::unlock(void *ptr)
{
    return false;
}
void BlockVolumeManager::waitForUnlock(void *ptr)
{
}
BlockVolumeManager::BlockVolumeManager()
{
}

MRAYNS_END

//
// Created by wyz on 2022/2/24.
//
#include "BlockVolumeManager.hpp"
#include <cassert>
#include <list>
#include <queue>
#include <mutex>
#include <condition_variable>
MRAYNS_BEGIN

struct BlockVolumeManager::BlockVolumeManagerImpl{
    struct MemoryBlock{
        void* data{nullptr};
        size_t size{0};
        bool isValid() const{
            return data && size;
        }
    };

    struct Lock{
        enum Type{
            NONE,READ_LOCK,WRITE_LOCK
        };

        Lock& operator+=(Type type){
            if(type==READ_LOCK){
                read_lock++;
            }
            else if(type == WRITE_LOCK){
                write_lock++;
            }
        }
        bool isLocked(){
            return isReadLocked() || isWriteLocked();
        }
        bool isReadLocked(){
            return read_lock;
        }
        bool isWriteLocked(){
            return write_lock;
        }
        int read_lock{0};
        int write_lock{0};
    };
    /**
     * each type of lock can just lock once
     */
    struct MemoryBlockDesc{
        MemoryBlock memory_block{};
        BlockIndex index{};

        Lock lock;
        size_t t{0};
        bool isLoaded() const{
            return index.isValid() && memory_block.data && memory_block.size>0;
        }
    };
    struct Pr{
        bool operator()(const MemoryBlockDesc& m1,const MemoryBlockDesc& m2) const{
            if(m1.memory_block.data != nullptr && m2.memory_block.data != nullptr){
                if(m1.t!=m2.t){
                    return m1.t > m2.t;//smaller t should priority to be used
                }
                else{
                    return m1.index.w > m2.index.w;//smaller lod should be replaced
                }
            }
            else if(m1.memory_block.data != nullptr && m2.memory_block.data ==nullptr){
                return false;
            }
            else if(m1.memory_block.data == nullptr && m2.memory_block.data !=nullptr){
                return true;
            }
            else{// both nullptr
                return true;
            }
        }
    };
    size_t memory_block_size{0};
    int memory_block_count{0};
    /**
     * 数据块可能存在于free_mem_blocks和locked_mem_blocks 前者是不稳定的 返回的指针指向的buffer会被更改
     * 后者的指针是稳定可靠的 里面的数据不会被更改
     */
    std::priority_queue<MemoryBlockDesc,std::vector<MemoryBlockDesc>> free_mem_blocks;
//    std::list<MemoryBlockDesc> free_mem_blocks;
    std::mutex free_mtx;
    std::list<MemoryBlockDesc> locked_mem_blocks;
    std::mutex locked_mtx;
    void create(int count,size_t mem_block_size){
        memory_block_count = count;
        memory_block_size = mem_block_size;
        assert(count && mem_block_size);
        std::lock_guard<std::mutex> lk(free_mtx);
        for(int i = 0;i<count;i++){
            MemoryBlockDesc desc;
            desc.memory_block.size = mem_block_size;
            desc.memory_block.data = ::operator new(mem_block_size);
            free_mem_blocks.push(desc);
        }
    }
    static size_t GetCurrentT(){
        static std::atomic<size_t> count;
        return ++count;
    }

    //从free_mem_blocks中获取一个数据块 如果free_mem_blocks是空的 则返回一个非法的MemoryBlock
    //否则 一定会返回一个MemoryBlock 但是其原来可能存储着数据
    //locked_mem_blocks里的数据无法被获取
    MemoryBlock getFreeMemoryBlock(Lock::Type lockType,BlockIndex index){
        if(free_mem_blocks.empty()) return MemoryBlock{};
        MemoryBlockDesc mem{};
        {
            std::lock_guard<std::mutex> lk(free_mtx);
            mem = free_mem_blocks.top();
            free_mem_blocks.pop();
            mem.index = index;
            mem.t  = GetCurrentT();
            assert(!mem.lock.isLocked());
            if(lockType == Lock::NONE){
                free_mem_blocks.push(mem);
                return mem.memory_block;
            }
        }

        std::lock_guard<std::mutex> lk(locked_mtx);
        mem.lock += lockType;
        assert(mem.lock.isLocked());
        locked_mem_blocks.push_back(mem);
        return mem.memory_block;
    }
    void releaseMemoryBlockLock(BlockIndex index){
        changeMemoryBlockLock(Lock::NONE,index);
    }
    //if change lock type force will always successfully while no force may failed
    //failed when none->read->write and force = false
    //successful when write->read->none
    bool changeMemoryBlockLock(Lock::Type,BlockIndex index,bool force = false){

    }
    //从free_mem_blocks和locked_mem_blocks中查询对应的Block 如果找到则返回对应的MemoryBlock并加指定的锁
    //如果没找到或者无法加锁 则返回一个非法的MemoryBlock
    MemoryBlock fetchMemoryBlock(Lock::Type lockType,BlockIndex index){
        //优先从locked_mem_blocks中查找
        auto locked_mem = fetchMemoryBlockFromLocked(lockType,index);
        if(locked_mem.isValid()) return locked_mem;

        auto free_mem = fetchMemoryBlockFromFree(lockType,index);
        if(locked_mem.isValid()) return free_mem;

        return MemoryBlock{};
    }
    MemoryBlock fetchMemoryBlockFromLocked(Lock::Type lockType,BlockIndex index){
        std::lock_guard<std::mutex> lk(locked_mtx);
        for(auto& mem:locked_mem_blocks){
            if(mem.index == index){

                if(!mem.lock.isLocked()){
                    assert(false);
                }
                if(mem.lock.isWriteLocked()){
                    //mem locked by WRITE_LOCK can't add a new lock read or write

                }
                else if(mem.lock.isReadLocked() && lockType==Lock::READ_LOCK){
                    //mem locked by READ_LOCK only can add a new read lock but can't add a write lock
                    mem.lock += lockType;
                    mem.t = GetCurrentT();
                    return mem.memory_block;
                }
                else{

                }
            }
        }
        return MemoryBlock{};
    }
    MemoryBlock fetchMemoryBlockFromFree(Lock::Type lockType,BlockIndex index){
        std::lock_guard<std::mutex> lk(free_mtx);
        decltype(free_mem_blocks) pq;
        MemoryBlock ret{};
        while(!free_mem_blocks.empty()){
            auto mem = free_mem_blocks.top();
            free_mem_blocks.pop();
            assert(!mem.lock.isLocked());
            if(mem.index == index){
                //should push this mem into locked_mem_blocks
                mem.t = GetCurrentT();
                mem.lock += lockType;
                std::lock_guard<std::mutex> tlk(locked_mtx);
                locked_mem_blocks.push_back(mem);
                ret = mem.memory_block;
            }
            else{
                pq.push(mem);
            }
        }
        free_mem_blocks = std::move(pq);
        return ret;
    }



};

BlockVolumeManager &mrayns::BlockVolumeManager::getInstance()
{
    static BlockVolumeManager block_volume_manager;
    return block_volume_manager;
}
void BlockVolumeManager::setProvider(std::unique_ptr<IVolumeBlockProviderInterface> &&provider)
{
    assert(provider.get());
    this->provider = std::move(provider);
    this->volume = provider->getVolume();
    assert(this->volume.isValid());
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
    return volume;
}
void *BlockVolumeManager::getVolumeBlock(const BlockIndex& blockIndex, bool sync)
{
    //1. query from cache if the block data is already cached
    auto block = impl->fetchMemoryBlock(BlockVolumeManagerImpl::Lock::READ_LOCK,blockIndex);
    if(block.isValid()) return block.data;
    //2. if not cached, request data from provider
    block = impl->getFreeMemoryBlock(BlockVolumeManagerImpl::Lock::WRITE_LOCK,blockIndex);
    //3. if sync wait for complete or async return immediately
    if(sync){
        provider->getVolumeBlock(block.data,blockIndex);
        bool ret = impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::READ_LOCK,blockIndex,true);
        assert(ret);
        return block.data;
    }
    else{
        //maybe a thread pool is a nice choice
        std::thread t([&](){
          provider->getVolumeBlock(block.data,blockIndex);
          bool ret = impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::READ_LOCK,blockIndex,true);
          assert(ret);
        });
        t.detach();
        return nullptr;
    }
}
void *BlockVolumeManager::getVolumeBlockAndLock(const BlockIndex& blockIndex)
{
    auto p = getVolumeBlock(blockIndex,true);
    if(p){
        waitForLock(p);
    }
    return p;
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
void BlockVolumeManager::init()
{

}
void BlockVolumeManager::destroy()
{

}
bool BlockVolumeManager::query(const BlockVolumeManager::BlockIndex &blockIndex)
{
    return false;
}

MRAYNS_END

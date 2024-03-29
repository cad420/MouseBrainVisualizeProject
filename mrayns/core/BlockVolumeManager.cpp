//
// Created by wyz on 2022/2/24.
//
#include "BlockVolumeManager.hpp"
#include <cassert>
#include <list>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "../common/Logger.hpp"
#include "../utils/Timer.hpp"
#include "../common/LRU.hpp"
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
        enum Type:int{
            NONE = 0,READ_LOCK = 1,WRITE_LOCK = 2
        };

        Lock& operator+=(Type type){
            if(type==READ_LOCK){
                read_lock++;
            }
            else if(type == WRITE_LOCK){
                if(write_lock > 0){
                    throw std::runtime_error("internal error: add write lock to wirte locked");
                }
                write_lock++;
            }
            return *this;
        }
        Lock& operator-=(Type type){
            if(type == READ_LOCK){
                read_lock = (max)(read_lock-1,0);
            }
            else if(type == WRITE_LOCK){
                write_lock--;
                if(write_lock){
                    throw std::runtime_error("internal error: decrease write lock but not equal to zero");
                }
            }
            return *this;
        }
        Lock& operator=(Type type){
            read_lock = 0;
            write_lock = 0;
            if(type == READ_LOCK){
                read_lock++;
            }
            else if(type == WRITE_LOCK){
                write_lock++;
            }
            return *this;
        }
        //failed when none->read->write
        //successful when write->read->none
        static bool isLockChangeValid(Type from, Type to){
            return to >= from;
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
        Type getLockType() const{
            assert( !(read_lock>0 && write_lock>0));
            if(read_lock > 0){
                assert(write_lock == 0);
                return READ_LOCK;
            }
            else if(write_lock > 0){
                return WRITE_LOCK;
            }
            else{
                return NONE;
            }
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
        bool operator==(const MemoryBlockDesc& other) const{
            return index == other.index;
        }
        bool operator!=(const MemoryBlockDesc& other) const{
            return !(*this == other);
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
     std::condition_variable cv;
     std::condition_variable loading_cv;
     std::mutex loading_mtx;
     std::mutex wait_mtx;
    std::priority_queue<MemoryBlockDesc,std::vector<MemoryBlockDesc>,Pr> free_mem_blocks;
//    LRUCache<BlockIndex,MemoryBlockDesc> free_mem_blocks;

//    std::list<MemoryBlockDesc> free_mem_blocks;
    std::mutex free_mtx;
    std::list<MemoryBlockDesc> locked_mem_blocks;
    std::mutex locked_mtx;

    std::unordered_map<void*,BlockIndex> buffer_map;
    std::mutex buffer_mtx;


    bool recordPtrForBlockIndex(void* ptr,const BlockIndex& index){
        std::lock_guard<std::mutex> lk(buffer_mtx);
        buffer_map[ptr] = index;
        return true;
    }
    BlockIndex getBlockIndexWithPtr(void* ptr){
        if(buffer_map.find(ptr) == buffer_map.end()){
            return BlockIndex{};
        }
        return buffer_map.at(ptr);
    }
    bool erasePtrRecording(void* ptr){
        std::lock_guard<std::mutex> lk(buffer_mtx);
        if(buffer_map.find(ptr)==buffer_map.end()){
            return false;
        }
        else{
            buffer_map.erase(ptr);
            return true;
        }
    }

    BlockVolumeManagerImpl(){
        //todo
        memory_block_size = 512 * 512 *512;
        memory_block_count = 64;
        for(int i = 0;i<memory_block_count;i++){
            MemoryBlockDesc desc;
            desc.memory_block.size = memory_block_size;
            desc.memory_block.data = ::operator new(memory_block_size);
            //must set to zero !!!
            memset(desc.memory_block.data,0,desc.memory_block.size);
            free_mem_blocks.push(desc);
        }
    };

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
        static std::atomic<size_t> count=0;
        return ++count;
    }

    //从free_mem_blocks中获取一个数据块
    // 如果free_mem_blocks是空的 则返回一个非法的MemoryBlock
    //否则 一定会返回一个MemoryBlock 但是其原来可能存储着数据
    //locked_mem_blocks里的数据无法被获取
    MemoryBlock getFreeMemoryBlock(Lock::Type lockType,const BlockIndex& index){
        int read_count = 0;
        {
            std::lock_guard<std::mutex> lk(locked_mtx);
            for(auto& mem:locked_mem_blocks){
                if(mem.lock.isReadLocked()) read_count++;
            }
        }
        LOG_INFO("free count {}, locked count {}, read lock count {}",free_mem_blocks.size(),locked_mem_blocks.size(),read_count);
        if(free_mem_blocks.empty()){
            std::unique_lock<std::mutex> lk(wait_mtx);
            cv.wait(lk,[&](){
                if(!free_mem_blocks.empty()){
                    return true;
                }
                else{
                    return false;
                }
            });
        }

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
    void releaseMemoryBlockLock(const BlockIndex& index){
        changeMemoryBlockLock(Lock::NONE,index);
    }
    //if change lock type force will always successfully while no force may failed
    //failed when none->read->write and force = false
    //successful when write->read->none
    //this not add new lock but change lock
    /**
     * @return -1 represent not find the memory block,
     * 0 represent failed and 1 represent successfully
     */
    int changeMemoryBlockLock(Lock::Type dstType,const BlockIndex& index,bool force = false){

        //memory block in free
        {
            std::unique_lock<std::mutex> lk(free_mtx);
            auto mem_desc = queryMemoryBlockFromFree(index);
            if(mem_desc.isLoaded()){
                assert(!mem_desc.lock.isLocked());
                if(dstType == mem_desc.lock.getLockType()) return 1; //mem in free to NONE lock just return is ok
                if(force || Lock::isLockChangeValid(mem_desc.lock.getLockType(),dstType)){
                    removeMemoryBlockDescInFree(mem_desc);
                    lk.unlock();
                    mem_desc.lock = dstType;
                    mem_desc.t = GetCurrentT();
                    std::lock_guard<std::mutex> lkk(locked_mtx);
                    appendMemoryBlockToLocked(mem_desc);
                    return 1;
                }
                else{
                    return 0;
                }
            }
        }
        //memory block in locked
        {
            std::unique_lock<std::mutex> lk(locked_mtx);
            auto mem_desc = queryMemoryBlockFromLocked(index);
            if(mem_desc.isLoaded()){
                assert(mem_desc.lock.isLocked());
                if(dstType == mem_desc.lock.getLockType()) return 1;
                if(force || Lock::isLockChangeValid(mem_desc.lock.getLockType(),dstType)){
                    if(dstType == Lock::NONE){
                        //move mem_desc from locked to free
                        removeMemoryBlockDescInLocked(mem_desc);
                        lk.unlock();
                        mem_desc.lock = dstType;
                        mem_desc.t = GetCurrentT();
                        std::lock_guard<std::mutex> lkk(free_mtx);
                        appendMemoryBlockToFree(mem_desc);
                    }
                    else{
                        //move mem_desc from locked to locked
                        removeMemoryBlockDescInLocked(mem_desc);
                        mem_desc.lock = dstType;
                        mem_desc.t = GetCurrentT();
                        appendMemoryBlockToLocked(mem_desc);
                    }
                    return 1;
                }
                else{
                    return 0;
                }
            }
        }
        //not find
        return -1;
    }
    //internal
    void removeMemoryBlockDescInFree(const MemoryBlockDesc& desc){
      decltype(free_mem_blocks) pq;
      while(!free_mem_blocks.empty()){
          if(free_mem_blocks.top() != desc)
              pq.push(free_mem_blocks.top());
          free_mem_blocks.pop();
      }
      free_mem_blocks = std::move(pq);
    }
    //internal
    void removeMemoryBlockDescInLocked(const MemoryBlockDesc& desc){
        for(auto it = locked_mem_blocks.begin();it!=locked_mem_blocks.end();it++){
            if(*it == desc){
                locked_mem_blocks.erase(it);
                return;
            }
        }
    }
    //internal
    void appendMemoryBlockToFree(const MemoryBlockDesc& desc){

        free_mem_blocks.push(desc);
        //notify wait for free memory block add
        cv.notify_one();
    }
    //public wait block appending
    void waitForBlockLoading(std::unique_lock<std::mutex>& lk,const BlockIndex& index){
        loading_cv.wait(lk,[this,index](){
            auto ret =  queryMemoryBlockFromLocked(index);
            return ret.isLoaded() && !ret.lock.isWriteLocked();
        });
    }
    //internal
    void appendMemoryBlockToLocked(const MemoryBlockDesc& desc){
        locked_mem_blocks.push_back(desc);
        LOG_DEBUG("append memory block to locked: {} {} {} {}, read lock count {}",desc.index.x,desc.index.y,desc.index.z,desc.index.w,desc.lock.read_lock);
        loading_cv.notify_all();
    }
    //internal
    MemoryBlockDesc queryMemoryBlockFromFree(const BlockIndex& index){
        decltype(free_mem_blocks) pq;
        MemoryBlockDesc ret{};
        while(!free_mem_blocks.empty()){
            auto mem = free_mem_blocks.top();
            free_mem_blocks.pop();
            if(mem.index == index){
                ret = mem;
            }
            pq.push(mem);
        }
        free_mem_blocks = std::move(pq);
        return ret;


    }
    //internal
    MemoryBlockDesc queryMemoryBlockFromLocked(const BlockIndex& index){
        auto p =_queryMemoryBlockFromLocked(index);
        if(p) return *p;
        else return MemoryBlockDesc{};
    }
    MemoryBlockDesc* _queryMemoryBlockFromLocked(const BlockIndex& index){
        MemoryBlockDesc* ret = nullptr;
        for(auto& mem:locked_mem_blocks){
            if(mem.index== index){
                return &mem;
            }
        }
        return ret;
    }

    bool isBlockMemoryWriting(const BlockIndex& index){
        auto ret = queryMemoryBlockFromLocked(index);
        return ret.isLoaded() && ret.lock.isWriteLocked();
    }
    //从free_mem_blocks和locked_mem_blocks中查询对应的Block 如果找到则返回对应的MemoryBlock并加指定的锁
    //如果没找到或者无法加锁 则返回一个非法的MemoryBlock
    //internal的函数是private的 一般不需要加锁 只需要在调用它们的public函数里加锁就行

    bool addMemoryBlockReadLock(const BlockIndex& index,Lock::Type type){
        if(type != Lock::READ_LOCK) return false;
        {
            std::unique_lock<std::mutex> free_lk(free_mtx);
            auto mem = queryMemoryBlockFromFree(index);
            free_lk.unlock();
            if(mem.isLoaded()){
                changeMemoryBlockLock(type,index,true);
                return true;
            }
        }
        {
            std::lock_guard<std::mutex> locked_lk(locked_mtx);
            auto p = _queryMemoryBlockFromLocked(index);
            if(p){
                assert(p->lock.isLocked());
                if(p->lock.isReadLocked()){
                    p->lock += type;
                    return true;
                }
                else{
                    LOG_CRITICAL("error: add lock to write lock");
                    return false;
                }
            }
        }
        return false;
    }
    bool reduceMemoryReadBlockLock(const BlockIndex& index,Lock::Type type){
        if(type != Lock::READ_LOCK) return false;
        {

            std::unique_lock<std::mutex> locked_lk(locked_mtx);
            auto pmem = _queryMemoryBlockFromLocked(index);
//            LOG_DEBUG("after query");
            if(pmem->isLoaded()){
                if(pmem->lock.isReadLocked()){
                    LOG_INFO("before read lock count {}",pmem->lock.read_lock);
                    pmem->lock -= type;
                    if(!pmem->lock.isReadLocked()){
//                        LOG_DEBUG("move from read to none");
                        auto mem = *pmem;
                        removeMemoryBlockDescInLocked(mem);
//                        LOG_DEBUG("after remove");
                        locked_lk.unlock();
                        std::unique_lock<std::mutex> free_lk(free_mtx);
//                        LOG_DEBUG("acquire free lock");
                        appendMemoryBlockToFree(mem);
//                        LOG_DEBUG("after append");
                        return true;
                    }
                    else{
                        return false;
                    }
                }
                else{
                    LOG_CRITICAL("error: add lock to write lock");
                }
            }
        }
    }
    MemoryBlock fetchMemoryBlock(Lock::Type lockType,const BlockIndex& index,bool wait = false){
        //优先从locked_mem_blocks中查找
        {
            std::unique_lock<std::mutex> lk(locked_mtx);
            auto locked_mem_desc = queryMemoryBlockFromLocked(index);
            if (locked_mem_desc.isLoaded())
            {
                auto locked_mem_desc = fetchMemoryBlockFromLocked(lockType,index);
//                assert(locked_mem.isValid());

                if(locked_mem_desc.isLoaded()){
                    if(locked_mem_desc.lock.isReadLocked()){
                        return locked_mem_desc.memory_block;
                    }
                    else if(locked_mem_desc.lock.isWriteLocked()){
                        if(!wait){
                            return MemoryBlock{};
                        }
                        else{
                            waitForBlockLoading(lk,index);
                            lk.unlock();

                            return locked_mem_desc.memory_block;
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(free_mtx);
            auto free_mem_desc = queryMemoryBlockFromFree(index);
            if(free_mem_desc.isLoaded()){
                auto free_mem = fetchMemoryBlockFromFree(lockType, index);
                assert(free_mem.isValid());
                if (free_mem.isValid())
                    return free_mem;
            }
        }
        return MemoryBlock{};
    }
    //internal
    MemoryBlockDesc fetchMemoryBlockFromLocked(Lock::Type lockType,const BlockIndex& index){
        auto p = _fetchMemoryBlockFromLocked(lockType,index);
        if(p) return *p;
        else return MemoryBlockDesc{};
    }
    MemoryBlockDesc* _fetchMemoryBlockFromLocked(Lock::Type lockType,const BlockIndex& index){
//        std::lock_guard<std::mutex> lk(locked_mtx);
        for(auto& mem:locked_mem_blocks){
            if(mem.index == index){

                if(!mem.lock.isLocked()){
                    assert(false);
                }
                if(mem.lock.isWriteLocked()){
                    //mem locked by WRITE_LOCK can't add a new lock read or write

                    mem.t = GetCurrentT();
                    return &mem;
                }
                else if(mem.lock.isReadLocked()){
                    //mem locked by READ_LOCK only can add a new read lock but can't add a write lock
                    mem.lock += lockType;
                    mem.t = GetCurrentT();
                    return &mem;
                }
                else{
                    throw std::runtime_error("unreachable");
                }
            }
        }
        return nullptr;
    }
    //internal

    MemoryBlock fetchMemoryBlockFromFree(Lock::Type lockType,const BlockIndex& index){
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
                //only locked will append to locked_mem_blocks
                if(mem.lock.isLocked()){
//                    std::lock_guard<std::mutex> tlk(locked_mtx);
                    locked_mem_blocks.push_back(mem);
                }
                else{
                    //if lockType is NONE then the block mem will push again into free_mem_blocks
                    pq.push(mem);
                }
                ret = mem.memory_block;
            }
            else{
                pq.push(mem);
            }
        }
        free_mem_blocks = std::move(pq);
        return ret;
    }


  private:
    MemoryBlockDesc createMemoryBlockDesc(){

    }

    MemoryBlock createMemoryBlock(){

    }

};

BlockVolumeManager& BlockVolumeManager::getInstance()
{
    static BlockVolumeManager block_volume_manager;
    return block_volume_manager;
}
void BlockVolumeManager::setProvider(std::unique_ptr<IVolumeBlockProviderInterface> &&provider)
{
    assert(provider.get());
    this->provider = std::move(provider);
    this->volume = this->provider->getVolume();
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
    bool writing = impl->isBlockMemoryWriting(blockIndex);
    if(writing && sync){
        auto block = impl->fetchMemoryBlock(BlockVolumeManagerImpl::Lock::NONE,blockIndex,sync);
        assert(block.isValid());
        return block.data;
    }
    else if(writing){
        LOG_DEBUG("block is writing: {} {} {} {}",blockIndex.x,blockIndex.y,blockIndex.z,blockIndex.w);
        return nullptr;
    }
    BlockVolumeManagerImpl::MemoryBlock block;
    {
        std::lock_guard<std::mutex> lk(get_mtx);
        // 1. query from cache if the block data is already cached
//        LOG_DEBUG("before fetch block: {} {} {} {}", blockIndex.x, blockIndex.y, blockIndex.z, blockIndex.w);
         block = impl->fetchMemoryBlock(BlockVolumeManagerImpl::Lock::NONE, blockIndex, sync);
//        LOG_DEBUG("after fetch block: {} {} {} {}", blockIndex.x, blockIndex.y, blockIndex.z, blockIndex.w);
        if (block.isValid())
        {
            impl->recordPtrForBlockIndex(block.data, blockIndex);
            LOG_DEBUG("record ptr for block: {} {} {} {}", blockIndex.x, blockIndex.y, blockIndex.z, blockIndex.w);
            return block.data;
        }
        if(impl->isBlockMemoryWriting(blockIndex)){
            return nullptr;
        }
        // 2. if not cached, request data from provider
        // 2.1 get free memory block buffer
        block = impl->getFreeMemoryBlock(BlockVolumeManagerImpl::Lock::WRITE_LOCK, blockIndex);
    }

//2.2 if sync wait for complete or async return immediately
//    LOG_INFO("4");
    if(sync){
        provider->getVolumeBlock(block.data,blockIndex);
        bool ret = impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::NONE,blockIndex,true);
        assert(ret);
        impl->recordPtrForBlockIndex(block.data, blockIndex);
//        LOG_INFO("5");
        return block.data;
    }
    else{
        //maybe a thread pool is a nice choice
        thread_pool.AppendTask([=](){
          LOG_INFO("start detach thread loading: {} {} {} {}",blockIndex.x,blockIndex.y,blockIndex.z,blockIndex.w);
          provider->getVolumeBlock(block.data,blockIndex);
          bool ret = impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::NONE,blockIndex,true);
          assert(ret);
          impl->recordPtrForBlockIndex(block.data, blockIndex);
          LOG_DEBUG("finish detach thread loading: {} {} {} {}",blockIndex.x,blockIndex.y,blockIndex.z,blockIndex.w);
          LOG_DEBUG("locked count: {}, free count: {}",impl->locked_mem_blocks.size(),impl->free_mem_blocks.size());
        });
        return nullptr;
    }
}
//该函数是同步的 会等待数据块加载完毕 因此对于writelock的数据块 它应该被知道 并且等待writelock的数据块加载完
void *BlockVolumeManager::getVolumeBlockAndLock(const BlockIndex& blockIndex)
{

    if(impl->isBlockMemoryWriting(blockIndex)){
        auto block = impl->fetchMemoryBlock(BlockVolumeManagerImpl::Lock::READ_LOCK, blockIndex, true);
        assert(block.isValid());
        impl->recordPtrForBlockIndex(block.data,blockIndex);
        return block.data;
    }

    BlockVolumeManagerImpl::MemoryBlock block;
    {
        std::lock_guard<std::mutex> lk(get_mtx);
        block = impl->fetchMemoryBlock(BlockVolumeManagerImpl::Lock::READ_LOCK, blockIndex, true);
        if (block.isValid())
        {
            impl->recordPtrForBlockIndex(block.data, blockIndex);
            return block.data;
        }
        block = impl->getFreeMemoryBlock(BlockVolumeManagerImpl::Lock::WRITE_LOCK, blockIndex);
    }

    START_TIMER
    provider->getVolumeBlock(block.data,blockIndex);

    STOP_TIMER("get volume block");

    bool ret = impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::READ_LOCK,blockIndex,true);
    assert(ret);

    impl->recordPtrForBlockIndex(block.data, blockIndex);

    return block.data;


}
bool BlockVolumeManager::lock(void *ptr)
{
    std::lock_guard<std::mutex> lk(lock_mtx);
    auto block_index = impl->getBlockIndexWithPtr(ptr);
    if(block_index.isValid()){
//        int ret = impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::READ_LOCK,block_index,true);
//        return ret == 1;
        LOG_DEBUG("before lock");
        bool e = impl->addMemoryBlockReadLock(block_index,BlockVolumeManagerImpl::Lock::READ_LOCK);
        LOG_DEBUG("after lock");
        if(!e) LOG_CRITICAL("lock fail");
        return e;
    }
    else{
        LOG_CRITICAL("lock failed");
        return false;
    }
}
void BlockVolumeManager::waitForLock(void *ptr)
{

}
bool BlockVolumeManager::unlock(void *ptr)
{
    std::lock_guard<std::mutex> lk(lock_mtx);
    auto block_index = impl->getBlockIndexWithPtr(ptr);
    if(block_index.isValid()){
//        impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::NONE,block_index,true);
        bool e = impl->reduceMemoryReadBlockLock(block_index,BlockVolumeManagerImpl::Lock::READ_LOCK);
        if(e){
            impl->erasePtrRecording(ptr);
        }
        LOG_INFO("unlock {} {} {} {}, free count {}",block_index.x,block_index.y,block_index.z,block_index.w,impl->free_mem_blocks.size());
        return true;
    }
    else{
        return false;
    }
}
void BlockVolumeManager::waitForUnlock(void *ptr)
{
    auto block_index = impl->getBlockIndexWithPtr(ptr);
    if(block_index.isValid()){
        impl->changeMemoryBlockLock(BlockVolumeManagerImpl::Lock::NONE,block_index,true);
        impl->erasePtrRecording(ptr);
    }
}
BlockVolumeManager::BlockVolumeManager()
:thread_pool(16)
{
    impl = std::make_unique<BlockVolumeManagerImpl>();
}
void BlockVolumeManager::init()
{

}
void BlockVolumeManager::destroy()
{

}
bool BlockVolumeManager::query(const BlockIndex &blockIndex)
{
    return false;
}


MRAYNS_END

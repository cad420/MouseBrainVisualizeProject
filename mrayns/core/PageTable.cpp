//
// Created by wyz on 2022/2/24.
//
#include "PageTable.hpp"
#include <unordered_map>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cassert>
#include "../common/Logger.hpp"
#include "../common/LRU.hpp"
#include <unordered_set>
MRAYNS_BEGIN

struct PageTable::Impl{
    Impl(){
        createLRU2();
    }
    ~Impl(){

    }
    struct LRU2{
        LRU2(int lru_size,int queue_size)
        :lru(lru_size),q_size(queue_size)
        {

        }
        LRU2()
        :lru(0),q_size(0)
        {}
        using Key = PageTable::ValueItem;
        using Value = PageTable::EntryItem;
        using Item = std::pair<Key,Value>;
        LRUCache<Key,Value> lru;
        std::queue<Item> q;
        int q_size;
        size_t size(){
            return q.size() + lru.get_size();
        }
        //无条件删除
        bool popItem(Key key,Item& item){
            int size = q.size();
            bool find = false;
            while(size-- > 0){
                if(q.front().first == key){
                    item = q.front();
                    q.pop();
                    find = true;
                }
                else{
                    auto t = q.front();
                    q.pop();
                    q.push(t);
                }
            }
            if(find) return true;
            auto res = lru.get_value_optional(key);
            if(res.has_value()){
                lru.pop_front();
                item.first = key;
                item.second = res.value();
                return true;
            }
            else{
                return false;
            }
        }

        //不会改变缓存优先级
        bool query(Key key){
            int size = q.size();
            bool find = false;
            while(size-- > 0){
                auto t = q.front();
                if(t.first == key){
                    find = true;
                }
                q.pop();
                q.push(t);
            }
            if(find) return true;
            auto res = lru.get_value_optional_without_move(key);
            return res.has_value();
        }
        //带改变缓存优先级的查询
        bool query(Key key,Value& value){
            int size = q.size();
            bool find = false;
            while(size-->0){
                auto item = q.front();
                q.pop();
                if(item.first == key){
                    value = item.second;
                    find = true;
                }
                else
                    q.push(item);
            }
            if(find){
                lru.emplace_back(key,value);
                return true;
            }
            else{
                auto res = lru.get_value_optional(key);
                if(res.has_value()){
                    value = res.value();
                    return true;
                }
                return false;
            }
        }

        void appendItem(Item item){
            //todo if check existed before append
//            if(query(item.first,item.second)) return;
            q.push(item);
            if(q.size() > q_size){
                q.pop();
            }

        }
        bool qfull(){
            return q_size == q.size();
        }
        bool full(){
            return q.size() == q_size && lru.get_load_factor() == 1.f;
        }
        bool empty(){
            return q.size() == 0 && lru.get_size() == 0;
        }
        Item eliminate(){
            if(!q.empty()){
                auto item = q.front();
                q.pop();
                return item;
            }
            else{
                if(lru.get_size()==0){
                    throw std::runtime_error("eliminate for empty lru-2");
                }
                auto item = lru.get_back();
                lru.pop_back();
                return item;
            }
        }

    };

    //https://segmentfault.com/a/1190000022558044

    using LockItem = std::pair<EntryItem,ValueItem>;
    using ReadLockItem = std::pair<LockItem,int>;
    using WriteLockItem = LockItem ;
    using CacheItem = LRU2::Item ;
    //MQ just with priority and without eliminate
    struct MQ{
        static constexpr int MinP = 0;
        static constexpr int MaxP = 6;
        using Key = LRU2::Key;
        using Value = LRU2::Value;
        using Item = LRU2::Item;
        std::map<int,LRU2> mq;
        MQ(){
            for(int i = MinP;i<=MaxP;i++){
                mq[i] = LRU2{192,192};
            }
        }
        size_t size(){
            size_t s = 0;
            for(int i = MinP;i<=MaxP;i++){
                s += mq[i].size();
            }
            return s;
        }
        void clear(){
            mq.clear();
        }
        int getPriority(Key key){
            assert(key.isValid());
            return std::clamp(key.w,MinP,MaxP);
        }

        bool valid(int p){
            return mq.find(p) != mq.end();
        }
        //会改变缓存优先级
        bool query(Key key,Value& value){
            //查询从高优先级开始
            for(int i = MaxP;i>=MinP;--i){
                auto res = mq[i].query(key,value);
                if(res) return true;
            }
            return false;
        }

        //不会改变缓存优先级
        bool query(Key key){
            //查询从高优先级开始
            for(int i = MaxP;i>=MinP;--i){
                auto res = mq[i].query(key);
                if(res) return true;
            }
            return false;
        }
        bool popItem(Key key,Item& item){
            //删除时从低优先级开始
            for(int i = MinP ;i<=MaxP;++i){
                auto res = mq[i].popItem(key,item);
                if(res) return true;
            }
            return false;
        }
        void appendItem(Item item,int p){
            if(p<MinP || p>MaxP){
                LOG_ERROR("invalid priority");
                return;
            }
            if(mq[p].full()){
                auto t = mq[p].eliminate();
                appendItem(t,p-1);
            }
            else{
                mq[p].appendItem(item);
                return;
            }
        }
        //todo if full?
        void appendItem(Item item){
            int p = getPriority(item.first);
            appendItem(item,p);
        }
        //must call not empty
        Item eliminate(){
            //淘汰时从低优先级开始
            for(int i = MinP;i<=MaxP;i++){
                if(!mq[i].empty()){
                    return mq[i].eliminate();
                }
            }
            throw std::runtime_error("eliminate for mq all is emtpy");
        }
    };
    struct{
        std::list<ReadLockItem> read_locked_items;
        std::list<WriteLockItem> write_locked_items;

        MQ cached_items;

        std::list<EntryItem> free_entries;
    }page_table;
    std::mutex acquire_mtx;
    std::mutex cache_mtx;//internal
    std::mutex read_mtx;
    std::mutex free_mtx;
    std::mutex write_mtx;
    std::condition_variable cache_cv;
    std::condition_variable read_cv;
    std::unordered_set<EntryItem> exist_entries;
    enum CacheStatus:int{
        UnCached = 0,Cached = 1,ReadLocked = 2,WriteLocked = 3,UnExist = 4
    };
    void createLRU2(){
        //todo
    }
    void lockCacheTable(){
        acquire_mtx.lock();
    }
    void unlockCacheTable(){
        acquire_mtx.unlock();
    }
    //internal
    CacheStatus queryValueItemStatus(ValueItem value)
    {
        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            if(page_table.cached_items.query(value)){
                return Cached;
            }
        }
        {
            std::lock_guard<std::mutex> lk(read_mtx);
            for(const auto& item:page_table.read_locked_items){
                if(item.first.second == value){
                    return ReadLocked;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(write_mtx);
            for(const auto& item:page_table.write_locked_items){
                if(item.second == value){
                    return WriteLocked;
                }
            }
        }
        return UnCached;
    }

    bool query(ValueItem value){
        auto status = queryValueItemStatus(value);
        return status == Cached || status == ReadLocked || status == WriteLocked;
    }

    bool insertEntryItem(EntryItem entry){
        if(exist_entries.find(entry)!=exist_entries.end()){
            return false;
        }
        else{
            exist_entries.insert(entry);
            std::lock_guard<std::mutex> lk(free_mtx);
            page_table.free_entries.push_back(entry);
            return true;
        }
    }
    //不管锁 直接全部清除
    void clearPageTable(){
        page_table.free_entries.clear();
        page_table.write_locked_items.clear();
        page_table.read_locked_items.clear();
        page_table.cached_items.clear();
    }

    void releaseLockedItem(ValueItem value){
        auto status = queryValueItemStatus(value);
        if(status == ReadLocked){
//            moveItem(value,ReadLocked,Cached);
            decreaseReadLockForExisted(value);
        }
        else if(status == WriteLocked){
            moveItem(value,WriteLocked,Cached);
        }
        else{
            LOG_ERROR("release a non-locked item");
        }
    }
    //write lock to read lock
    //otherwise not current write lock will return false
    void downLockedItem(ValueItem value){
        auto status = queryValueItemStatus(value);
        if(status!=WriteLocked) return;
        moveItem(value,WriteLocked,ReadLocked);
    }


    bool popFromReadLock(ValueItem value,LockItem& lockItem){
        std::lock_guard<std::mutex> lk(read_mtx);
        for(auto it = page_table.read_locked_items.begin();it!=page_table.read_locked_items.end();it++){
            if(it->first.second == value){
                lockItem = it->first;
                page_table.read_locked_items.erase(it);
                return true;
            }
        }
        return false;
    }
    bool popFromWriteLock(ValueItem value,LockItem& lockItem){
        std::lock_guard<std::mutex> lk(write_mtx);
        for(auto it = page_table.write_locked_items.begin();it!=page_table.write_locked_items.end();it++){
            if(it->second == value){
                lockItem = *it;
                page_table.write_locked_items.erase(it);
                return true;
            }
        }
        return false;
    }
    bool popFromCache(ValueItem value,CacheItem& item){
        std::lock_guard<std::mutex> lk(cache_mtx);
        return page_table.cached_items.popItem(value,item);
    }
    void pushToReadLock(LockItem lockItem){
        assert(queryValueItemStatus(lockItem.second)!=ReadLocked);
        std::lock_guard<std::mutex> lk(read_mtx);
        page_table.read_locked_items.emplace_back(lockItem,1);
        read_cv.notify_all();
    }
    void pushToWriteLock(LockItem lockItem){
//        assert(queryValueItemStatus(lockItem.second)!=WriteLocked);
        std::lock_guard<std::mutex> lk(write_mtx);
        page_table.write_locked_items.emplace_back(lockItem);
    }
    void pushToCache(CacheItem item){
        std::lock_guard<std::mutex> lk(cache_mtx);

        page_table.cached_items.appendItem(item);

        cache_cv.notify_one();
    }

    //只要找到 那么就无条件地移动
    //only for items in cached, read locked or write locked but not for free entries
    void moveItem(ValueItem value,CacheStatus from,CacheStatus to){
        assert(queryValueItemStatus(value) == from);
        assert(to != UnExist && to!= UnCached);
        assert(from != UnExist && from!= UnCached);
        if(from == to) return;
        if(from == Cached){
            CacheItem cacheItem{};
            bool res = popFromCache(value,cacheItem);
            assert(res);
            if(to == ReadLocked){
                pushToReadLock(LockItem{cacheItem.second,cacheItem.first});
            }
            else if(to == WriteLocked){
                pushToWriteLock(LockItem{cacheItem.second,cacheItem.first});
            }
        }
        else if(from == ReadLocked){
            LockItem lockItem{};
            auto res = popFromReadLock(value,lockItem);
            assert(res);
            if(to == Cached){
                pushToCache(CacheItem{lockItem.second,lockItem.first});
            }
            else if(to == WriteLocked){
                pushToWriteLock(lockItem);
            }
        }
        else if(from == WriteLocked){
            LockItem lockItem{};
            auto res = popFromWriteLock(value,lockItem);
            assert(res);
            if(to == Cached){
                pushToCache(CacheItem{lockItem.second,lockItem.first});
            }
            else if(to == ReadLocked){
                pushToReadLock(lockItem);
            }
        }
        else{
            assert(false);
        }
    }

    //internal
    //add lock for item already in the read_lock_items
    //if not exist return false else add read lock count and return true
    bool addReadLockForExisted(ValueItem value){
        std::lock_guard<std::mutex> lk(read_mtx);
        for(auto& item:page_table.read_locked_items){
            if(item.first.second == value){
                assert(item.second > 0);
                ++item.second;
                return true;
            }
        }
        return false;
    }
    bool decreaseReadLockForExisted(ValueItem value){
        bool move = false;
        {
            std::lock_guard<std::mutex> lk(read_mtx);
            for (auto &item : page_table.read_locked_items)
            {
                if (item.first.second == value)
                {
                    assert(item.second > 0);
                    if (item.second == 1)
                    {
                        move = true;
                        break;
                    }
                    else
                    {
                        --item.second;
                        return true;
                    }
                }
            }
        }
        if(move){
            moveItem(value,ReadLocked,Cached);
            return true;
        }
        else return false;
    }

    bool queryItemAndReadLock(ValueItem value){
        auto status = queryValueItemStatus(value);
        if(status==ReadLocked){
            addReadLockForExisted(value);
            return true;
        }
        else if(status == Cached){
            moveItem(value,Cached,ReadLocked);
            return true;
        }
        else{
            return false;
        }
    }

    std::vector<EntryItemExt> queryItemsAndReadLock(const std::vector<ValueItem>& values){
        std::vector<EntryItemExt> ret;
        for(const auto& value:values){
            auto status = queryValueItemStatus(value);
            if(status != ReadLocked && status != Cached){
                ret.emplace_back(EntryItemExt{EntryItem{},value,false});
                continue;
            }
            if(status == ReadLocked){
                addReadLockForExisted(value);
            }
            else if(status == Cached){
                moveItem(value,Cached,ReadLocked);
            }
            std::lock_guard<std::mutex> lk(read_mtx);
            for(auto& item:page_table.read_locked_items){
                if(item.first.second == value){
                    ret.emplace_back(EntryItemExt{item.first.first,item.first.second,true});
                }
            }
        }
        return ret;
    }

    //当一个Item被Write Lock时 其它线程无法得知 会再次Write Lock
    //所以会造成在Read Lock里有可能有两个相同的ValueItem
    //由于ReadLock列表是由list存储的 因此可以存储两个或多个相同的值
    //此时如果再追加ReadLock或者将其ReadLock减一 优先加大的 优先减小的
    //但是上面的问题可以很好地解决
    //因为在调用getEntriesAndWriteLock的时候 可以得知所有Write Lock的信息
    //那么此时WriteLock对于我们来说是已知的 就不应该再重新分配Entry并WriteLock
    //而是等待其WriteLock变为ReadLock时 也就是说 有其他调用者会上传 当前调用者需要等待

    //调用该函数时 可能所需的ValueItem在ReadLock里或者Cached里找到
    //此时应该返回额外的信息表明这些数据已经缓存了 并且将其Read Lock
    //std::vector<std::pair<EntryItem,bool>> true代表需要上传更新 false代表已经缓存
    std::vector<EntryItemExt> getEntriesAndWriteLock(const std::vector<ValueItem>& values){

        std::vector<EntryItemExt> ret;
        //1. 查询是否存在于WriteLock链表中
        //理论上  notify_one 通知的lk优先级更大 因为notify_one发生了 lk稀释之前???
//        LOG_INFO("111");
        std::vector<std::thread> tasks;
        std::vector<ValueItem> remains_after_write;
        for(const auto& value:values){
            //1.1 如果存在 那么等待其变为ReadLock之后将其ReadLock加1
            if(queryValueItemStatus(value) == WriteLocked){
                tasks.emplace_back([this,value,&ret](){
                   std::unique_lock<std::mutex> lk(read_mtx);
                   read_cv.wait(lk,[this,value,&ret](){
                       for(auto& item:page_table.read_locked_items){
                           if(item.first.second == value){
                               item.second++;
                               ret.emplace_back(EntryItemExt{item.first.first,item.first.second,true});
                               return true;
                           }
                       }
                       return false;
                   });
                });
            }
            else{
                remains_after_write.emplace_back(value);
            }
        }
        for(auto& task:tasks) if(task.joinable()) task.join();
        if(remains_after_write.empty()) return ret;
        //2. 查询是否存在于ReadLock中
        LOG_INFO("222");
        std::vector<ValueItem> remains_after_read;
        {
            std::lock_guard<std::mutex> lk(read_mtx);
            for (const auto &value : remains_after_write)
            {
                bool find = false;
                for(auto& item:page_table.read_locked_items){
                    if(item.first.second == value){
                        item.second++;
                        ret.emplace_back(EntryItemExt{item.first.first,item.first.second,true});
                        find = true;
                    }
                }
                if(!find){
                    remains_after_read.emplace_back(value);
                }
            }
        }
        if(remains_after_read.empty()) return ret;
        //3. 从Cache中获取EntryItem 在单线程中 可不用考虑
        LOG_INFO("333");
        std::vector<ValueItem> remains_after_cached;
        {
            std::lock_guard<std::mutex> lk(cache_mtx);

            for(const auto& value:remains_after_read){
                LRU2::Item item{};
                if(page_table.cached_items.popItem(value,item)){
                    pushToReadLock({item.second,item.first});
                    ret.emplace_back(EntryItemExt{item.second,item.first,true});
                }
                else{
                    remains_after_cached.emplace_back(value);
                }
            }
        }
        if(remains_after_cached.empty()) return ret;
        //4. 从free entries中获取entry用于上传
        LOG_INFO("444");
        LOG_INFO("write lock count {}",page_table.write_locked_items.size());
        LOG_INFO("read lock count {}",page_table.read_locked_items.size());
        LOG_INFO("cached count {}",page_table.cached_items.size());
        LOG_INFO("free count {}",page_table.free_entries.size());
        std::vector<ValueItem> remains_after_free;
        {
            std::lock_guard<std::mutex> lk(free_mtx);
            for(const auto& value:remains_after_cached){
                if(!page_table.free_entries.empty()){
                    auto entry = page_table.free_entries.front();
                    ret.emplace_back(EntryItemExt{entry,value,false});
                    pushToWriteLock({entry,value});
                    page_table.free_entries.pop_front();
                }
                else{
                    remains_after_free.emplace_back(value);
                }
            }
        }
        if(remains_after_free.empty()) return ret;
        //5. 淘汰Cache中某些Item从而获取EntryItem用于上传
        LOG_INFO("555");

        {
            std::unique_lock<std::mutex> lk(cache_mtx);

            auto cached_size = page_table.cached_items.size();
            auto needed_size = remains_after_read.size();
            if (needed_size > cached_size)
            {
                cache_cv.wait(lk, [&]() {
                    LOG_INFO("{} {}",page_table.cached_items.size(),needed_size);
                    return page_table.cached_items.size() > needed_size;
                });
            }

            for(const auto& value:remains_after_free){
                auto item = page_table.cached_items.eliminate();
                ret.emplace_back(EntryItemExt{item.second,value,false});
                pushToWriteLock({item.second,value});
            }
        }
        LOG_INFO("666");
        assert(ret.size() == values.size());
        return ret;
    }


};

void PageTable::insert(EntryItem entry)
{

    bool res = impl->insertEntryItem(entry);
    if(!res){
        throw std::runtime_error("PageTable insert an already exist entry item");
    }

}
void PageTable::clear()
{
    impl->clearPageTable();
}
static thread_local size_t locked_id = 0;
bool IsAcquireLocked(){
    return locked_id;
}
void PageTable::acquireLock()
{
    if(std::hash<std::thread::id>()(std::this_thread::get_id()) == locked_id){
        throw std::runtime_error("lock for already locked mtx");
    }
    impl->lockCacheTable();
    locked_id = std::hash<std::thread::id>()(std::this_thread::get_id());
}
void PageTable::acquireRelease()
{
//    LOG_INFO("call PageTable::unlock");
    assert(locked_id);
    impl->unlockCacheTable();
    locked_id = 0;
}
bool PageTable::query(ValueItem value)
{
    return impl->query(value);
}
bool PageTable::queryAndLock(ValueItem value)
{
    assert(IsAcquireLocked());
    return impl->queryItemAndReadLock(value);
}
PageTable::EntryItemExt PageTable::getEntryAndLock(ValueItem value)
{
    return getEntriesAndLock({value}).front();
}
std::vector<PageTable::EntryItemExt> PageTable::getEntriesAndLock(const std::vector<ValueItem> &values)
{
    assert(IsAcquireLocked());
    return impl->getEntriesAndWriteLock(values);
}
//no locked
void PageTable::update(ValueItem value)
{
    impl->downLockedItem(value);
}
//no locked
void PageTable::release(ValueItem value)
{
    impl->releaseLockedItem(value);
}
PageTable::~PageTable()
{

}
PageTable::PageTable()
{
    impl = std::make_unique<Impl>();
}
PageTable::EntryItemExt PageTable::queryAndLockExt(ValueItem value)
{
    assert(IsAcquireLocked());
    return queriesAndLockExt({value}).front();
}
std::vector<PageTable::EntryItemExt> PageTable::queriesAndLockExt(const std::vector<ValueItem>& values)
{
    return impl->queryItemsAndReadLock(values);
}

MRAYNS_END

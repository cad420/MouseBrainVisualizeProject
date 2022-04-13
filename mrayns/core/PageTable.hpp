//
// Created by wyz on 2022/2/23.
//
#pragma once

#include "Volume.hpp"
MRAYNS_BEGIN

/**
 * PageTable代表的是物理地址存储的数据块索引
 */
class PageTable{
  public:

    struct EntryItem{
        int x;
        int y;
        int z;
        int w;
        bool operator==(const EntryItem& entry) const{
            return x == entry.x && y == entry.y && z == entry.z && w == entry.w;
        }
        explicit operator bool() const{
            return x < 0 || y < 0 || z < 0 || w < 0;
        }
        bool isValid() const{
            return bool(*this);
        }
        EntryItem()
        :x(-1),y(-1),z(-1),w(-1)
        {}
        EntryItem(int x,int y,int z,int w):
        x(x),y(y),z(z),w(w)
        {}
    };


    using ValueItem = Volume::BlockIndex;


    //add new key
    //will throw exception if exists
    void insert(EntryItem);

    void clear();

    //lock for entire PageTable for multithreading context
    //因为如果不把page table整个锁住 那么每个线程的渲染器都可以同时获取page table的entry
    //但是由于GPU纹理资源有限 而且每个渲染器需要的资源比较多 会造成GPU资源无法同时满足所有渲染器
    //所有渲染器同时卡住等待资源的情况 这对于BlockVolumeManager来说是一个问题 因为它采用这种设计模式

    //只是对自由的页表项加锁 即只对以下两个函数操作加锁 queryAndLock getEntryAndLock
    void acquireLock();
    void acquireRelease();

    //no lock
    //用于预先判断处理 包括write lock的项
    bool query(ValueItem);

    //查询ValueItem是否存储当中 如果存在则将其加锁并返回true 否则返回false
    //加了write lock的不能算存储了 因为它还没有上传
    //add read lock
    bool queryAndLock(ValueItem);

    //add write lock
    //will wait
    //会考虑加了write lock的 避免重复上传
    struct EntryItemExt{
        EntryItem entry;
        ValueItem value;
        bool cached;
    };

    EntryItemExt queryAndLockExt(ValueItem);

    std::vector<EntryItemExt> queriesAndLockExt(const std::vector<ValueItem>& );

    bool queryCached(const ValueItem&);

    EntryItemExt getEntryAndLock(ValueItem);

    //get all entries the same time and lock all
    std::vector<EntryItemExt> getEntriesAndLock(const std::vector<ValueItem>& );

    //no need lock first
    //update ValueItem with write lock to read lock
    void update(ValueItem);

    //no need lock first
    //release read or write lock
    //write -> cached but read may still be read locked
    void release(ValueItem);

    //构造和析构函数都要见到Impl的完整定义
    PageTable();
    ~PageTable();
  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
MRAYNS_END

namespace std{
    template <>
    struct hash<mrayns::PageTable::EntryItem>{
        size_t operator()(const mrayns::PageTable::EntryItem& entry) const{
            return mrayns::hash(entry.x,entry.w,entry.z,entry.w);
        }
    };
}
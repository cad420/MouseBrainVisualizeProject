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

    };


    using ValueItem = Volume::BlockIndex;

    /**
     * 更新PageTable内部记录的Entry和Value项
     * 需要在向GPUResource上传资源后手动更新
     */
    void update(EntryItem,ValueItem);


    bool query(ValueItem,EntryItem&);

    /**
     * 锁定某个物理地址 其存储的数据块索引无法更改 无法被缓存策略选中
     * 如果已经被锁定 则返回false 否则加锁成功则返回true
     */
    bool lock(EntryItem);

    /**
     * @return true represent this value is already exits in the page table,
     *false represent this is getting by cache policy
     */
    bool getEntryItem(ValueItem,EntryItem&);

    void release(EntryItem);

    /**
     * 释放所有对EntryItem的锁定
     */
    void releaseAll();

  private:

};
MRAYNS_END
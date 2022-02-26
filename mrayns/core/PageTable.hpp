//
// Created by wyz on 2022/2/23.
//
#pragma once

#include "Volume.hpp"
MRAYNS_BEGIN
class PageTable{
  public:

    struct EntryItem{

    };


    using ValueItem = Volume::BlockIndex;

    void update(EntryItem,ValueItem);


    bool query(ValueItem,EntryItem&);

    bool lock(Volume::BlockIndex);

    /**
     * @return true represent this value is already exits in the page table,
     *false represent this is getting by cache policy
     */
    bool getEntryItem(ValueItem,EntryItem&);

    bool release(EntryItem);

    bool releaseAll();

  private:

};
MRAYNS_END
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

    struct ValueItem{

    };

    void update(EntryItem,ValueItem);

    EntryItem query(ValueItem);


  private:

};
MRAYNS_END
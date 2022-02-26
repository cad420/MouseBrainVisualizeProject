//
// Created by wyz on 2022/2/24.
//
#include "PageTable.hpp"
MRAYNS_BEGIN
void PageTable::update(PageTable::EntryItem, PageTable::ValueItem)
{
}
bool PageTable::query(PageTable::ValueItem, PageTable::EntryItem &)
{
    return false;
}
bool PageTable::lock(Volume::BlockIndex)
{
    return false;
}
bool PageTable::getEntryItem(PageTable::ValueItem, PageTable::EntryItem &)
{
    return false;
}
bool PageTable::release(PageTable::EntryItem)
{
    return false;
}
bool PageTable::releaseAll()
{
    return false;
}

MRAYNS_END

//
// Created by wyz on 2022/2/24.
//
#include "PageTable.hpp"
MRAYNS_BEGIN
void PageTable::update(EntryItem, ValueItem)
{
}
bool PageTable::query(ValueItem, EntryItem &)
{
    return false;
}
bool PageTable::lock(EntryItem)
{
    return false;
}
bool PageTable::getEntryItem(ValueItem, EntryItem &)
{
    return false;
}
void PageTable::release(PageTable::EntryItem)
{

}
void PageTable::releaseAll()
{

}

MRAYNS_END

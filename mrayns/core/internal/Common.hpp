//
// Created by wyz on 2022/4/7.
//
#pragma once
#include "../PageTable.hpp"
#include <stdexcept>
MRAYNS_BEGIN
namespace internal
{

struct MappingTable
{
    static constexpr int HashTableSize = 1024; // the same in the shader

    // max uniform buffer size 64kb
    using HashTableItem = std::pair<PageTable::ValueItem, PageTable::EntryItem>;
    using HashTableKey = HashTableItem::first_type;
    static constexpr int HashTableItemSize = sizeof(HashTableItem);
    struct HashTable
    {
        HashTableItem hash_table[HashTableSize];
        uint32_t hash(const HashTableKey &key)
        {
            static_assert(sizeof(HashTableKey) == sizeof(int) * 4, "");
            uint32_t *v = (uint32_t *)(&key);
            uint32_t value = v[0];
            for (int i = 1; i < 4; i++)
            {
                value = value ^ (v[i] + 0x9e3779b9 + (value << 6) + (value >> 2));
            }
            return value;
        }
        void append(const HashTableItem &item)
        {
            // glsl not support 64bit
            //            size_t hash_v = std::hash<HashTableKey>()(item.first);
            uint32_t hash_v = hash(item.first);
            int pos = hash_v % HashTableSize;
            int i = 0;
            bool positive = false;
            while (true)
            {
                int ii = i * i;
                pos += positive ? ii : -ii;
                pos %= HashTableSize;
                if (!hash_table[pos].first.isValid())
                {
                    hash_table[pos] = item;
                    return;
                }
                if (!positive)
                    i++;
                positive = !positive;
                if (i > HashTableSize)
                {
                    throw std::runtime_error("hash table can't find empty packet");
                }
            }
        }

        void clear()
        {
            for (int i = 0; i < HashTableSize; i++)
            {
                hash_table[i].first = {-1, -1, -1, -1};
            }
        }
    };
};

}
MRAYNS_END
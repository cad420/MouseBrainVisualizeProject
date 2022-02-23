//
// Created by wyz on 2022/2/23.
//
#pragma once

#include "PageTable.hpp"
#include <vector>
class GPUNode{
  public:
    GPUNode& getInstance();
    void setGPUIndex(int index);
    int getGPUIndex() const;

    enum OperationType{
        Read,Write
    };

    using TableValueItem = PageTable::ValueItem;

    OperationType getOperationType(const TableValueItem&);

    OperationType getOperationType(const std::vector<TableValueItem>&);

    void addReadLock(const TableValueItem&);
    void addReadLock(const std::vector<TableValueItem>&);

    void addWriteLock(const TableValueItem&);
    void addWriteLock(const std::vector<TableValueItem>&);

    int getReadLockNum();

    int getWriteLockNum();

    /**
     * @brief GPUNode only has one PageTable to record gpu memory usage.
     */
    PageTable& getPageTable();
  private:
    GPUNode();

    int gpu_index;
};

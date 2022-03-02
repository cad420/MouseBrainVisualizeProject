//
// Created by wyz on 2022/2/23.
//
#pragma once

#include "PageTable.hpp"
#include <vector>
MRAYNS_BEGIN
/**
 * @brief Used for render task on GPU and not used for codec
 * GPUNode应该绑定到GPUResource里
 * GPUNode的决策都是基于内嵌的PageTable
 * GPUNode是分割任务算法的依据
 */
class GPUNode{
  public:
    explicit GPUNode(int index);

    int getGPUIndex() const;

    /**
     * Write means it should modify data in the GPU.
     * Read means it will not need new data to upload to GPU.
     */
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
    PageTable page_table;
    int gpu_index;
};
MRAYNS_END
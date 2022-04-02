//
// Created by wyz on 2022/2/23.
//
#pragma once

#include <vector>
#include "../common/Define.hpp"
#include <memory>
MRAYNS_BEGIN
/**
 * @brief Class for store cpu and gpu information.
 * Not provide any schedule idea.
 * 主机节点 包括了CPU和GPU的任务调度情况
 */
class HostNode{
  public:
    //single instance for one program
    static HostNode& getInstance();

    void setGPUNum(int num);
    int getGPUNum() const;
    void getCPUMemInfo(int& total,int& free);//GB
    void getGPUMemInfo(int GPUIndex,int& total,int& free);

    using GPUCap = size_t;
    GPUCap getGPUCap(int GPUIndex);
    enum Type:int{
        Codec=0,Graphic=1,Compute=2
    };
    struct CPUTask{
        Type type;
        int time_cost = 0;
        int thread_count = 0;
        int memory_cost = 0;
        bool isValid() const{
            return time_cost > 0 && thread_count >0 && memory_cost >0 && type != Graphic;
        }
    };
    struct GPUTask{

        Type type;
        int time_cost{0};
        int resource_cost{0};
        //memory is already allocated for gpu task so memory is not care about
//        int memory_cost{0};
        int gpu_index{-1};
        bool isValid() const{
            return gpu_index >= 0 && time_cost > 0
//                   && memory_cost>0
                   && resource_cost>0;
        }
    };
    using TaskHandle = size_t;
    /**
     * @brief Just record a descriptive information(GPUTask) which a real GPU task is submitted by others.
     * @return
     */
    TaskHandle recordGPUTask(GPUTask task);

    void eraseGPUTask(TaskHandle handle);

    std::vector<GPUTask> getGPUTasks(int GPUIndex,Type type);

    std::vector<GPUTask> getGPUTasks(int GPUIndex);

    std::vector<GPUTask> getGPUTasks(Type type);

    std::vector<GPUTask> getGPUTasks();
  private:
    HostNode();

    struct Impl;
    std::unique_ptr<Impl> impl;

    int gpu_num{0};
};
MRAYNS_END
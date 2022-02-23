//
// Created by wyz on 2022/2/23.
//
#pragma once
#include <map>
#include <vector>
/**
 * @brief Class for store cpu and gpu information.
 * Not provide any schedule idea.
 */
class HostNode{
  public:
    HostNode& getInstance();

    void setGPUNum();
    int getGPUNum() const;
    void getCPUMemInfo(int& total,int& free);//GB
    void getGPUMemInfo(int& total,int& free);

    struct GPUTask{
        enum Type{
            Codec=0,Graphic=1,Compute=2
        };
        Type type;
        int time_cost;
        int resource_cost;
        int memory_cost;
        int gpu_index;
    };
    using TaskDesc = size_t;
    /**
     * @brief Just record a descriptive information(GPUTask) which a real GPU task is submitted by others.
     * @return
     */
    TaskDesc recordGPUTask(GPUTask task);

    void eraseGPUTask(TaskDesc desc);

    std::vector<GPUTask> getAllGPUTasks();
  private:
    HostNode();

    TaskDesc generateTaskDesc();

    std::map<TaskDesc,GPUTask> gpu_tasks;
};

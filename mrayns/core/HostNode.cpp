//
// Created by wyz on 2022/2/24.
//
#include "HostNode.hpp"
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include <cassert>
MRAYNS_BEGIN

struct HostNode::Impl{

    std::unordered_map<TaskHandle,GPUTask> gpu_tasks;
    std::mutex task_mtx;

    TaskHandle appendTask(GPUTask task){
        std::lock_guard<std::mutex> lk(task_mtx);
        auto handle = GetT();
        assert(gpu_tasks.find(handle) == gpu_tasks.end());
        gpu_tasks[handle] = task;
        return handle;
    }
    void removeTask(TaskHandle handle){
        std::lock_guard<std::mutex> lk(task_mtx);
        gpu_tasks.erase(handle);
    }

    std::vector<GPUTask> getGPUTask(Type type,int GPUIndex){
        std::lock_guard<std::mutex> lk(task_mtx);
        std::vector<GPUTask> tasks;
        for(auto& item:gpu_tasks){
            if(item.second.type == type && item.second.gpu_index == GPUIndex){
                tasks.emplace_back(item.second);
            }
        }
        return tasks;
    }

  private:
    static size_t GetT(){
        static size_t t;
        return ++t;
    }
};

HostNode& HostNode::getInstance()
{
    static HostNode host_node;
    return host_node;
}
HostNode::HostNode()
{
    impl = std::make_unique<Impl>();
}
void HostNode::setGPUNum(int num)
{
    this->gpu_num = num;
}
int HostNode::getGPUNum() const
{
    return gpu_num;
}
void HostNode::getCPUMemInfo(int &total, int &free)
{

}
void HostNode::getGPUMemInfo(int GPUIndex,int &total, int &free)
{

}
HostNode::TaskHandle HostNode::recordGPUTask(GPUTask task)
{
    return impl->appendTask(task);
}
void HostNode::eraseGPUTask(HostNode::TaskHandle handle)
{
    impl->removeTask(handle);
}
std::vector<HostNode::GPUTask> HostNode::getGPUTasks()
{
    std::vector<GPUTask> tasks;
    for(int i = 0;i<gpu_num;i++){
        for(int j = 0;j<3;j++){
            auto task = impl->getGPUTask(static_cast<Type>(j),i);
            tasks.insert(tasks.end(),task.begin(),task.end());
        }
    }

    return tasks;
}
std::vector<HostNode::GPUTask> HostNode::getGPUTasks(int GPUIndex, Type type)
{
    return impl->getGPUTask(type,GPUIndex);
}
std::vector<HostNode::GPUTask> HostNode::getGPUTasks(int GPUIndex)
{
    std::vector<GPUTask> tasks;
    for(int i = 0;i<3;i++){
        auto task = impl->getGPUTask(static_cast<Type>(i),GPUIndex);
        tasks.insert(tasks.end(),task.begin(),task.end());
    }
    return tasks;
}
std::vector<HostNode::GPUTask> HostNode::getGPUTasks(Type type)
{
    std::vector<GPUTask> tasks;
    for(int i = 0;i<3;i++){
        auto task = impl->getGPUTask(type,i);
        tasks.insert(tasks.end(),task.begin(),task.end());
    }
    return tasks;
}

MRAYNS_END

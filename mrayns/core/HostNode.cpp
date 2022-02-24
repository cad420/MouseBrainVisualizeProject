//
// Created by wyz on 2022/2/24.
//
#include "HostNode.hpp"
MRAYNS_BEGIN
HostNode &mrayns::HostNode::getInstance()
{
    static HostNode host_node;
    return host_node;
}
HostNode::HostNode()
{
}
void HostNode::setGPUNum(int num)
{
}
int HostNode::getGPUNum() const
{
    return 0;
}
void HostNode::getCPUMemInfo(int &total, int &free)
{
}
void HostNode::getGPUMemInfo(int &total, int &free)
{
}
HostNode::TaskDesc HostNode::recordGPUTask(HostNode::GPUTask task)
{
    return 0;
}
void HostNode::eraseGPUTask(HostNode::TaskDesc desc)
{
}
std::vector<HostNode::GPUTask> HostNode::getAllGPUTasks()
{
    return std::vector<GPUTask>();
}
HostNode::TaskDesc HostNode::generateTaskDesc()
{
    return 0;
}

MRAYNS_END

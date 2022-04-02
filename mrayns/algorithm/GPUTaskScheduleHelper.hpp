//
// Created by wyz on 2022/3/1.
//
#pragma once
#include "../core/HostNode.hpp"
MRAYNS_BEGIN
struct GPUTaskScheduleHelper{


    //evaluate a gpu task burden for codec task
    //not care about task's memory cost
    //care for resource cost and resource_cost * time_cost
    //care for already commit codec task
    static int DefaultGPUTaskCodecEvaluator(const HostNode::GPUTask& task){
        assert(task.isValid());
        int burden = 0;
        if(task.type == HostNode::Codec){
            burden += task.resource_cost * task.time_cost * 2;
        }
        else if(task.type == HostNode::Graphic){
            burden += task.time_cost * task.resource_cost * 1;
        }
        else if(task.type == HostNode::Compute){
            burden += task.time_cost * task.resource_cost * 1;
        }
        return burden;
    }

    //evaluate a gpu task burden for graphic task
    static int DefaultGPUTaskGraphicEvaluator(const HostNode::GPUTask& task){
        assert(task.isValid());
        int burden = 0;
        if(task.type == HostNode::Codec){
            burden += task.resource_cost * task.time_cost * 1;
        }
        else if(task.type == HostNode::Graphic){
            burden += task.time_cost * task.resource_cost * 3;
        }
        else if(task.type == HostNode::Compute){
            burden += task.time_cost * task.resource_cost * 2;
        }
        return burden;
    }

    //evaluate a gpu task burden for compute task
    static int DefaultGPUTaskComputeEvaluator(const HostNode::GPUTask& task){
        assert(task.isValid());
        int burden = 0;
        if(task.type == HostNode::Codec){
            burden += task.resource_cost * task.time_cost * 1;
        }
        else if(task.type == HostNode::Graphic){
            burden += task.time_cost * task.resource_cost * 2;
        }
        else if(task.type == HostNode::Compute){
            burden += task.time_cost * task.resource_cost * 3;
        }
        return burden;
    }

    //default meanings think of all the cost include time, resource and memory
    //this is not suit for all gpu task like decode task is not care about memory cost but pay attention to resource cost
    static int DefaultGPUTaskEvaluator(const HostNode::GPUTask& task){
        assert(task.isValid());
        int burden = 0;
        int ratio = 0;
        if(task.type == HostNode::Codec){
            ratio = 1;
        }
        else if(task.type == HostNode::Graphic){
            ratio = 2;
        }
        else if(task.type == HostNode::Compute){
            ratio = 2;
        }
        burden += ratio * task.resource_cost * task.time_cost;
        return burden;
    }

    static int GetOptimalGPUIndex(HostNode* hostNode){
        return GetOptimalGPUIndex(hostNode,DefaultGPUTaskEvaluator);
    }

    static int GetOptimalGPUIndex(HostNode* hostNode,std::function<int(const HostNode::GPUTask&)> evaluator){
        auto gpu_tasks = hostNode->getGPUTasks();
        std::vector<int> burden(hostNode->getGPUNum(),0);
        for(auto& task:gpu_tasks){
            assert(task.isValid());
            burden[task.gpu_index] += evaluator(task);
        }
        auto idx = std::min_element(burden.begin(),burden.end()) - burden.begin();
        return idx;
    }
};

MRAYNS_END
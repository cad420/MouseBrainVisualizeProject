//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "common/Define.hpp"
#include <string>
MRAYNS_BEGIN

class IPluginFactory
{
  public:
    virtual std::string Key() const = 0;
    virtual void *Create(const std::string &key) = 0;
    virtual std::string GetModuleID() const = 0;
    virtual ~IPluginFactory() = default;
};
using GetPluginFactory = IPluginFactory *(*)();

MRAYNS_END
//
// Created by wyz on 2022/2/24.
//
#pragma once
#include "../common/Define.hpp"
#include <string>

MRAYNS_BEGIN

class EXPORT Library{
  public:
    Library(const std::string& name);
    void* Symbol(const std::string& name) const;
    void Close();
    ~Library();
  private:
    void* lib;
};

MRAYNS_END

//
// Created by wyz on 2022/2/24.
//
#pragma once

#if defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS)
#define WINDOWS
#else
#define LINUX
#endif

#define MRAYNS_BEGIN namespace mrayns{
#define MRAYNS_END }

#ifdef WINDOWS
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

#ifdef NDEBUG
#define DEBUG_WINDOWW
#endif



//
// Created by wyz on 2022/2/24.
//
#include "Library.hpp"
#include "../common/Logger.hpp"
#include <stdexcept>
#ifdef WINDOWS
#include <Windows.h>
#elif defined(LINUX)
#include <dlfcn.h>
#include <sys/times.h>
#endif

MRAYNS_BEGIN

Library::Library(const std::string &name) : lib(nullptr)
{
    std::string error_msg;
#if defined(_WIN32)
    lib = LoadLibrary(TEXT(name.c_str()));
    if (!lib)
    {
        auto err = GetLastError();
        LPTSTR lpMsgBuf;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                      err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
        error_msg = lpMsgBuf;
        LocalFree(lpMsgBuf);
    }
#elif defined(__linux__)

#endif
    if (!lib)
    {
        LOG_ERROR(name + " not found!");
        throw std::runtime_error(error_msg);
    }
}

void *Library::Symbol(const std::string &name) const
{
    assert(lib);
#if defined(WINDOWS)
    return GetProcAddress(reinterpret_cast<HMODULE>(lib), name.c_str());
#elif defined(LINUX)

#endif
}

void Library::Close()
{
#if defined(WINDOWS)
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#elif defined(LINUX)

#endif
}

Library::~Library()
{
    Close();
}

MRAYNS_END
//
// Created by wyz on 2022/2/23.
//
#pragma once
#include "common/Define.hpp"

#define EXPORT_PLUGIN_FACTORY_DECL(plugin_factory_typename)                                                            \
    extern "C" EXPORT ::mrayns::IPluginFactory *GetPluginFactoryInstance();

#define EXPORT_PLUGIN_FACTORY_IMPL(plugin_factory_typename)                                                            \
    ::mrayns::IPluginFactory *GetPluginFactoryInstance()                                                                   \
    {                                                                                                                  \
        return GetHelper_##plugin_factory_typename();                                                                  \
    }

#define REGISTER_PLUGIN_FACTORY_DECL(plugin_factory_typename)                                                       \
    extern "C" ::mrayns::IPluginFactory *GetHelper_##plugin_factory_typename();

#define REGISTER_PLUGIN_FACTORY_IMPL(plugin_factory_typename)                                                       \
    ::mrayns::IPluginFactory *GetHelper_##plugin_factory_typename()                                                        \
    {                                                                                                                  \
        static plugin_factory_typename factory;                                                                        \
        return &factory;                                                                                               \
    }

#define DECLARE_PLUGIN_MODULE_ID(plugin_interface_typename, module_id)                                                 \
    template <> struct module_id_traits<plugin_interface_typename>                                                     \
    {                                                                                                                  \
        static std::string GetModuleID()                                                                               \
        {                                                                                                              \
            return module_id;                                                                                      \
        }                                                                                                              \
    };

MRAYNS_BEGIN
template <typename T> struct module_id_traits;
MRAYNS_END
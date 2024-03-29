//
// Created by wyz on 2022/2/24.
//
#include "LibraryReposity.hpp"
#ifdef WINDOWS
#include <filesystem>
#elif defined(LINUX)
#include <experimental/filesystem>
#endif
#include <regex>
#include "../common/Logger.hpp"

MRAYNS_BEGIN


class LibraryReposityImpl
{
  public:
    static LibraryReposity *instance;
    std::unordered_map<std::string, std::shared_ptr<Library>> repo;
};

LibraryReposity *LibraryReposityImpl::instance = nullptr;
static LibraryReposity *GetInstance()
{
    return LibraryReposityImpl::instance;
}

#define GetRepo() GetInstance()->impl->repo

// private construct
LibraryReposity::LibraryReposity()
{
    this->impl = std::make_unique<LibraryReposityImpl>();
}

LibraryReposity::~LibraryReposity()
{
    LOG_INFO("Destruct of LibraryReposity.");
}

LibraryReposity *LibraryReposity::GetLibraryRepo()
{
    if (!GetInstance())
        LibraryReposityImpl::instance = new LibraryReposity();
    return GetInstance();
}

void LibraryReposity::AddLibrary(const std::string &path)
{

    std::string full_name ;
    std::string::size_type pos1 = path.find_last_of('/');
    std::string::size_type pos2 = path.find_last_of('\\');
    auto pos = std::max(pos1,pos2) + 1;
    if(pos == std::string::npos){
        throw std::runtime_error("AddLibrary: Invalid path "+path);
    }
    full_name = path.substr(pos);
    if (full_name.empty())
    {
        LOG_ERROR("AddLibrary pass wrong format path:{0}", path);
        return;
    }
    auto lib_name = GetLibraryName(full_name);
    if (lib_name.empty())
    {
        LOG_ERROR("{0} is not valid library", path);
        return;
    }
    if (GetRepo().find(lib_name) != GetRepo().end())
    {
        LOG_INFO("{0} has been loaded", lib_name);
    }
    try
    {
        auto lib = std::make_shared<Library>(path);
        auto &repo = GetRepo();
        repo.insert({lib_name, lib});
    }
    catch (const std::exception &err)
    {
        LOG_ERROR(err.what());
    }
}

void LibraryReposity::AddLibraries(const std::string &directory)
{
#ifdef _WIN32
    try
    {
        for (auto &lib : std::filesystem::directory_iterator(directory))
        {
            AddLibrary(lib.path().string());
        }
    }
    catch (const std::filesystem::filesystem_error &err)
    {
        LOG_ERROR("No such directory: {0}, {1}.", directory, err.what());
    }
#else
    try
    {
        for (auto &lib : std::experimental::filesystem::directory_iterator(directory))
        {
            AddLibrary(lib.path().string());
        }
    }
    catch (const std::experimental::filesystem::filesystem_error &err)
    {
        LOG_ERROR("No such directory: {0}, {1}.", directory, err.what());
    }
#endif
}

void *LibraryReposity::GetSymbol(const std::string &symbol)
{
    void *sym = nullptr;
    auto &repo = GetRepo();
    for (auto it = repo.cbegin(); it != repo.cend(); it++)
    {
        sym = it->second->Symbol(symbol);
        if (sym)
            return sym;
    }
    return sym;
}

void *LibraryReposity::GetSymbol(const std::string &lib, const std::string &symbol)
{
    void *sym = nullptr;
    auto &repo = GetRepo();
    auto it = repo.find(lib);
    if (it != repo.end())
        sym = it->second->Symbol(symbol);
    return sym;
}

bool LibraryReposity::Exists(const std::string &lib) const
{
    auto &repo = GetRepo();
    return repo.find(lib) != repo.end();
}

auto LibraryReposity::GetLibrepo() const -> const std::unordered_map<std::string, std::shared_ptr<Library>>
{
    return GetInstance()->impl->repo;
}

std::string EXPORT GetLibraryName(const std::string &full_name)
{
    std::regex reg;
    std::string lib_name = full_name.substr(0, full_name.find_last_of('.'));
#ifdef _WIN32
    reg = std::regex(R"(.+\.dll$)");
    if (std::regex_match(full_name, reg))
        return full_name;
#elif defined(__linux__)

#endif
    return "";
}

std::string EXPORT MakeValidLibraryName(const std::string &name)
{
    std::string full_name;
#ifdef _WIN32
    full_name = name + ".dll";
#elif defined(__linux__)

#endif
    return full_name;
}

MRAYNS_END
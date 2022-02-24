//
// Created by wyz on 2022/2/24.
//
#pragma once
#include "Library.hpp"
#include <unordered_map>
#include <memory>
MRAYNS_BEGIN
class LibraryReposityImpl;
class EXPORT LibraryReposity
    {
        public:
        ~LibraryReposity();

        static LibraryReposity *GetLibraryRepo();

        void AddLibrary(const std::string &path);

        void AddLibraries(const std::string &directory);

        void *GetSymbol(const std::string &symbol);

        void *GetSymbol(const std::string &lib, const std::string &symbol);

        bool Exists(const std::string &lib) const;

        auto GetLibrepo() const -> const std::unordered_map<std::string, std::shared_ptr<Library>>;

        private:
        LibraryReposity();
        std::unique_ptr<LibraryReposityImpl> impl;
    };

std::string EXPORT GetLibraryName(std::string const &);

std::string EXPORT MakeValidLibraryName(std::string const &);

MRAYNS_END
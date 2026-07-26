#pragma once

#include <Windows.h>
#include <string>

enum class BrowsePathKind
{
    File,
    Folder
};

struct BrowsePathResult
{
    BrowsePathKind kind{BrowsePathKind::Folder};
    std::wstring folderpath;
    std::wstring filepath;
    DWORD win32_error{ERROR_SUCCESS};

    bool Succeeded() const
    {
        return win32_error == ERROR_SUCCESS;
    }
};

BrowsePathResult ClassifyBrowsePath(const std::wstring& path);
bool BrowsePathsShareFolder(const std::wstring& first, const std::wstring& second);

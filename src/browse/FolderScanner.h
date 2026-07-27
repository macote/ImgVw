#pragma once

#include "ImgItem.h"

#include <Windows.h>

#include <functional>
#include <string>
#include <vector>

struct FolderScanCallbacks
{
    std::function<bool()> cancelled;
    std::function<void(const std::wstring&, ImgItem::Format)> file_found;
    std::function<void(const std::wstring&)> folder_found;
};

enum class FolderScanStatus
{
    Completed,
    Cancelled,
    EnumerationFailed
};

struct FolderScanResult
{
    FolderScanStatus status{FolderScanStatus::Completed};
    DWORD win32_error{ERROR_SUCCESS};

    bool Succeeded() const
    {
        return status == FolderScanStatus::Completed;
    }

    bool Cancelled() const
    {
        return status == FolderScanStatus::Cancelled;
    }
};

class FolderScanner final
{
  public:
    FolderScanResult Scan(const std::wstring& folderpath, bool recursive, const FolderScanCallbacks& callbacks) const;
    FolderScanResult ScanFolders(const std::vector<std::wstring>& folderpaths,
                                 const FolderScanCallbacks& callbacks) const;

  private:
    FolderScanResult ScanFolder(const std::wstring& folderpath, bool recursive,
                                const FolderScanCallbacks& callbacks) const;
};

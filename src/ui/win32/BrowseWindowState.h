#pragma once

#include <string>
#include <utility>

class BrowseWindowState final
{
  public:
    BrowseWindowState() = default;
    explicit BrowseWindowState(std::wstring path) : path_(std::move(path)) {}

    const std::wstring& path() const
    {
        return path_;
    }

    bool browser_initialized() const
    {
        return browser_initialized_;
    }

    bool browse_subfolders() const
    {
        return browse_subfolders_;
    }

    bool CanStartSubfolderSearch() const
    {
        return browser_initialized_ && !browse_subfolders_;
    }

    void MarkBrowserInitialized()
    {
        browser_initialized_ = true;
    }

    void OpenPath(const std::wstring& path)
    {
        path_ = path;
        browse_subfolders_ = false;
    }

    void EnableSubfolderSearch()
    {
        browse_subfolders_ = true;
    }

  private:
    std::wstring path_;
    bool browser_initialized_{};
    bool browse_subfolders_{};
};

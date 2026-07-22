#pragma once

#include "ImgFileList.h"
#include "ImgCache.h"
#include "ImgLoader.h"
#include <Windows.h>
#include <memory>
#include <string>
#include <vector>

struct ImgBrowserStats
{
    std::size_t found_images{};
    ImgFileListProgress sequential;
    ImgFileListProgress random;
    INT targetwidth{};
    INT targetheight{};
    ImgLoaderStats loader;
    std::vector<ImgCacheSizeStats> sizes;
};

struct ImgBrowserLoadContext
{
    std::shared_ptr<ImgCache> cache{std::make_shared<ImgCache>()};
    std::shared_ptr<ImgLoader> loader{std::make_shared<ImgLoader>()};

    void Clear()
    {
        loader->DiscardQueuedItems();
        cache->Clear();
    }
};

enum class ImgBrowserStopStatus
{
    AlreadyStopped,
    Stopped,
    SignalFailed,
    TimedOut,
    WaitFailed
};

struct ImgBrowserOperationStopResult
{
    ImgBrowserStopStatus status{ImgBrowserStopStatus::AlreadyStopped};
    DWORD win32_error{ERROR_SUCCESS};

    bool Stopped() const
    {
        return status == ImgBrowserStopStatus::AlreadyStopped || status == ImgBrowserStopStatus::Stopped;
    }
};

struct ImgBrowserStopResult
{
    ImgBrowserOperationStopResult collection;
    ImgBrowserOperationStopResult target_queue;

    bool Stopped() const
    {
        return collection.Stopped() && target_queue.Stopped();
    }
};

class ImgBrowserCore;

class ImgBrowser final
{
  public:
    ImgBrowser();
    ~ImgBrowser();
    ImgBrowser(const ImgBrowser&) = delete;
    ImgBrowser& operator=(const ImgBrowser&) = delete;
    void ShareLoadContext(const std::shared_ptr<ImgBrowserLoadContext>& context);
    std::shared_ptr<ImgBrowserLoadContext> loadcontext() const;
    BOOL BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight, BOOL clearloadcontext = FALSE);
    BOOL UpdateTargetSize(INT targetwidth, INT targetheight);
    BOOL BrowseSubFoldersAsync();
    BOOL IsCollectingComplete() const;
    DWORD GetCollectionError();
    BOOL HasFiles();
    ImgBrowserStopResult StopBrowsing();
    void SetNotificationWindow(HWND hwnd, UINT message);
    std::wstring GetCurrentFilePath();
    std::shared_ptr<ImgItem> GetCurrentItem();
    BOOL MoveToNext();
    BOOL MoveToPrevious();
    BOOL MoveToFirst();
    BOOL MoveToLast();
    BOOL MoveToItem(const std::wstring& filepath);
    BOOL MoveToOrAddItem(const std::wstring& filepath);
    void BeginRandomCycle();
    void PreloadFrom(ImgBrowser& source);
    BOOL MoveToRandom();
    BOOL MoveToRandomExcluding(const std::vector<std::wstring>& excluded);
    void RemoveCurrentItem();
    void ReloadCurrentItem();
    BOOL PreloadTargetSize(INT targetwidth, INT targetheight);
    BOOL PreloadTargetSizes(const std::vector<SIZE>& target_sizes);
    ImgBrowserStats GetStats();
    ImgFileListProgress GetSequentialProgress(const std::wstring& filepath);

  private:
    std::shared_ptr<ImgBrowserCore> core_;
};

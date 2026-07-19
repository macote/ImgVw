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

class ImgBrowser final
{
  public:
    ImgBrowser() : loadcontext_(std::make_shared<ImgBrowserLoadContext>())
    {
        readyevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!InitializeCriticalSectionAndSpinCount(&browsecriticalsection_, 0x00000400))
        {
            // TODO: handle error
        }
    }
    ~ImgBrowser()
    {
        StopBrowsing();
        DeleteCriticalSection(&browsecriticalsection_);
        CloseHandle(readyevent_);
        if (collectorthread_ != NULL)
        {
            CloseHandle(collectorthread_);
        }
    }
    ImgBrowser(const ImgBrowser&) = delete;
    ImgBrowser& operator=(const ImgBrowser&) = delete;
    void ShareLoadContext(const std::shared_ptr<ImgBrowserLoadContext>& context);
    std::shared_ptr<ImgBrowserLoadContext> loadcontext() const
    {
        return loadcontext_;
    }
    BOOL BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight, BOOL clearloadcontext = FALSE);
    BOOL UpdateTargetSize(INT targetwidth, INT targetheight);
    BOOL BrowseSubFoldersAsync();
    BOOL IsCollectingComplete() const;
    void StopBrowsing();
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
    struct TargetSize
    {
        INT width{};
        INT height{};
    };
    struct TargetSizeQueueRequest
    {
        ImgBrowser* browser{};
        std::vector<TargetSize> sizes;
        BOOL loadnext{};
    };
    struct PathQueueRequest
    {
        ImgBrowser* browser{};
        std::vector<std::wstring> paths;
        INT targetwidth{};
        INT targetheight{};
    };

    std::shared_ptr<ImgBrowserLoadContext> loadcontext_;
    BOOL cancellationflag_{};
    BOOL recursive_{};
    std::wstring folderpath_;
    ImgFileList files_;
    std::vector<std::wstring> folders_;
    HANDLE collectorthread_{NULL};
    std::vector<HANDLE> targetqueuethreads_;
    HANDLE readyevent_;
    CRITICAL_SECTION browsecriticalsection_;
    INT targetwidth_{};
    INT targetheight_{};
    std::vector<TargetSize> preload_target_sizes_;
    HWND notificationhwnd_{nullptr};
    UINT notificationmessage_{};

  private:
    void CollectFile(const std::wstring& filepath, ImgItem::Format imgformat);
    void CollectFolder(const std::wstring& folderpath);
    void CollectSubFolders();
    BOOL StopCollecting();
    BOOL StopTargetQueueing();
    void NotifyChanged();
    static DWORD WINAPI StaticThreadCollect(void* browserinstance);
    static DWORD WINAPI StaticThreadCollectSubFolders(void* browserinstance);
    static DWORD WINAPI StaticThreadQueueTargetSize(void* targetsizequeuerequest);
    static DWORD WINAPI StaticThreadQueuePaths(void* pathqueuerequest);
    void Reset();
    BOOL AddPreloadTargetSize(INT targetwidth, INT targetheight);
    BOOL AddPreloadTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes);
    BOOL IsTargetSizeActiveLocked(INT targetwidth, INT targetheight) const;
    void QueueTargetSizeAsync(INT targetwidth, INT targetheight, BOOL loadnext);
    void QueueTargetSizesAsync(const std::vector<TargetSize>& target_sizes, BOOL loadnext);
    void QueueTargetSizes(const std::vector<TargetSize>& target_sizes, BOOL loadnext);
    void QueuePathsAsync(std::vector<std::wstring> paths, INT targetwidth, INT targetheight);
    void QueuePaths(const std::vector<std::wstring>& paths, INT targetwidth, INT targetheight);
    void QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext);
    void CleanupTargetQueueThreads();
    ImgItem::Format ResolveFileFormat(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                                   ImgItem::Format imgformat);
};

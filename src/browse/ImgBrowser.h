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
    INT targetwidth{};
    INT targetheight{};
    ImgLoaderStats loader;
    std::vector<ImgCacheSizeStats> sizes;
};

class ImgBrowser final
{
  public:
    ImgBrowser()
    {
        readyevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!InitializeCriticalSectionAndSpinCount(&browsecriticalsection_, 0x00000400))
        {
            // TODO: handle error
        }
    }
    ~ImgBrowser()
    {
        StopCollecting();
        StopTargetQueueing();
        DeleteCriticalSection(&browsecriticalsection_);
        CloseHandle(readyevent_);
        if (collectorthread_ != NULL)
        {
            CloseHandle(collectorthread_);
        }
    }
    ImgBrowser(const ImgBrowser&) = delete;
    ImgBrowser& operator=(const ImgBrowser&) = delete;
    void BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight);
    BOOL UpdateTargetSize(INT targetwidth, INT targetheight);
    void BrowseSubFoldersAsync();
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
    BOOL MoveToRandom();
    void RemoveCurrentItem();
    void ReloadCurrentItem();
    BOOL PreloadTargetSize(INT targetwidth, INT targetheight);
    BOOL PreloadTargetSizes(const std::vector<SIZE>& target_sizes);
    ImgBrowserStats GetStats();

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

    ImgCache cache_;
    ImgLoader loader_;
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
    std::vector<TargetSize> target_sizes_;
    HWND notificationhwnd_{nullptr};
    UINT notificationmessage_{};

  private:
    void CollectFile(const std::wstring& filepath, ImgItem::Format imgformat);
    void CollectFolder(const std::wstring& folderpath);
    void CollectSubFolders();
    void StopCollecting();
    void StopTargetQueueing();
    void NotifyChanged();
    static DWORD WINAPI StaticThreadCollect(void* browserinstance);
    static DWORD WINAPI StaticThreadCollectSubFolders(void* browserinstance);
    static DWORD WINAPI StaticThreadQueueTargetSize(void* targetsizequeuerequest);
    void Reset();
    BOOL AddTargetSize(INT targetwidth, INT targetheight);
    BOOL AddTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes);
    void QueueTargetSizeAsync(INT targetwidth, INT targetheight, BOOL loadnext);
    void QueueTargetSizesAsync(const std::vector<TargetSize>& target_sizes, BOOL loadnext);
    void QueueTargetSizes(const std::vector<TargetSize>& target_sizes, BOOL loadnext);
    void QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext);
    void CleanupTargetQueueThreads();
    ImgItem::Format ResolveFileFormat(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                                   ImgItem::Format imgformat);
};

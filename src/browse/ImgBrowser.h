#pragma once

#include "ImgCache.h"
#include "ImgLoader.h"
#include <Windows.h>
#include <string>
#include <set>
#include <vector>
#include <iterator>
#include <limits>
#include <memory>
#include <random>

class ImgBrowser final
{
  public:
    static const DWORD kIndexPark = 0x80000000;

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
    BOOL MoveToRandom();
    void RemoveCurrentItem();
    void ReloadCurrentItem();

  private:
    ImgCache cache_;
    ImgLoader loader_;
    BOOL cancellationflag_{};
    BOOL recursive_{};
    std::wstring folderpath_;
    std::set<std::wstring>::iterator currentfileiterator_;
    std::set<std::wstring> files_;
    std::vector<std::wstring> randomlist_;
    std::vector<std::wstring> folders_;
    DWORD currentrandomindex_{kIndexPark};
    std::random_device rd_;
    std::mt19937 rnge_{rd_()};
    HANDLE collectorthread_{NULL};
    HANDLE readyevent_;
    CRITICAL_SECTION browsecriticalsection_;
    INT targetwidth_{};
    INT targetheight_{};
    HWND notificationhwnd_{nullptr};
    UINT notificationmessage_{};

  private:
    void CollectFile(const std::wstring& filepath);
    void CollectFolder(const std::wstring& folderpath);
    void CollectSubFolders();
    void StopCollecting();
    void NotifyChanged();
    static DWORD WINAPI StaticThreadCollect(void* browserinstance);
    static DWORD WINAPI StaticThreadCollectSubFolders(void* browserinstance);
    void Reset();
    BOOL IsFileFormatSupported(LPCTSTR fileName);
};

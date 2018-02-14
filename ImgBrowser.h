#pragma once

#include "ImgCache.h"
#include "ImgLoader.h"
#include <Windows.h>
#include <string>
#include <set>
#include <vector>
#include <iterator>
#include <limits>
#include <random>

class ImgBrowser final
{
public:
    static const DWORD kIndexPark = 0x80000000;
public:
    ImgBrowser()
    {
        if (!InitializeCriticalSectionAndSpinCount(&browsecriticalsection_, 0x00000400))
        {
            // TODO: handle error
        }
    }
    ~ImgBrowser()
    {
        StopCollecting();
        DeleteCriticalSection(&browsecriticalsection_);
        if (collectorthread_ != NULL)
        {
            CloseHandle(collectorthread_);
        }
    }
    ImgBrowser(const ImgBrowser&) = delete;
    ImgBrowser& operator=(const ImgBrowser&) = delete;
    void StartBrowsingAsync(const std::wstring& path, INT targetwidth, INT targetheight);
    void StopBrowsing();
    std::wstring GetCurrentFilePath();
    const ImgItem* GetCurrentItem();
    BOOL MoveToNext();
    BOOL MoveToPrevious();
    BOOL MoveToFirst();
    BOOL MoveToLast();
    BOOL MoveToItem(std::wstring filepath);
    BOOL MoveToRandom();
    void RemoveCurrentItem();
private:
    ImgCache cache_;
    ImgLoader loader_;
    BOOL cancellationflag_{};
    std::wstring folderpath_;
    std::set<std::wstring>::iterator currentfileiterator_;
    std::set<std::wstring> files_;
    std::vector<std::wstring> randomlist_;
    DWORD currentrandomindex_{ kIndexPark };
    std::random_device rd_;
    std::mt19937 rnge_{ rd_() };
    HANDLE collectorthread_{ NULL };
    CRITICAL_SECTION browsecriticalsection_;
    INT targetwidth_{};
    INT targetheight_{};
private:
    void CollectFile(const std::wstring& filepath);
    DWORD CollectFolder(const std::wstring& folderpath);
    void StopCollecting();
    static DWORD WINAPI StaticThreadCollect(void* browserinstance);
    void Reset();
    BOOL IsFileFormatSupported(LPCTSTR fileName);
};
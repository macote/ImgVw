#pragma once

#include "ImgCache.h"
#include "ImgLoader.h"
#include <Windows.h>
#include <set>
#include <vector>
#include <iterator>
#include <limits>
#include <random>

class ImgBrowser
{
public:
    static const DWORD kIndexPark = 0x80000000;
public:
    ImgBrowser()
    {
        InitializeTempPath();
        cache_.set_temppath(temppath_);
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

        DeleteTempPath();
    }
    void StartBrowsingAsync(std::wstring path, INT targetwidth, INT targetheight);
    void StopBrowsing();
    std::wstring GetCurrentFilePath();
    ImgItem* GetCurrentItem();
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
    std::wstring temppath_;
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
    void InitializeTempPath();
    void DeleteTempPath();
    void CollectFile(std::wstring filepath);
    DWORD CollectFolder(std::wstring folderpath);
    void StopCollecting();
    static DWORD WINAPI StaticThreadCollect(void* browserinstance);
    void Reset();
    BOOL IsFileFormatSupported(LPCTSTR fileName);
};
#pragma once

#include "ImgItem.h"
#include "CountingSemaphore.h"
#include <Windows.h>
#include <list>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class LoaderItem
{
public:
    LoaderItem(ImgItem* imgitem, std::function<void()> handler)
        : imgitem_(imgitem), loadcompleteevent_(handler) { }
    ~LoaderItem()
    {
        CloseLoaderItemThread();
    }
    LoaderItem(const LoaderItem&) = delete;
    LoaderItem& operator=(const LoaderItem&) = delete;
    ImgItem* imgitem() const { return imgitem_; }
    HANDLE loaderitemthread() const { return loaderitemthread_; }
    void set_loaderitemthread(HANDLE loaderitemthread) { loaderitemthread_ = loaderitemthread; }
    void CloseLoaderItemThread()
    {
        if (loaderitemthread_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(loaderitemthread_);
            loaderitemthread_ = INVALID_HANDLE_VALUE;
        }
    }
    void LoadComplete()
    {
        if (loadcompleteevent_ != nullptr)
        {
            loadcompleteevent_();
        }
    }
private:
    ImgItem* imgitem_{ nullptr };
    HANDLE loaderitemthread_{ INVALID_HANDLE_VALUE };
    std::function<void()> loadcompleteevent_{ nullptr };
};

class ImgLoader
{
public:
    static constexpr auto kMaximumLoaderCount = 2;	// TODO: adjust logic around this limit once GDI+ gets replaced completely
    static constexpr auto kCleanupCycleCountTrigger = 29;
public:
    ImgLoader()
    {
        if (!InitializeCriticalSectionAndSpinCount(&queuecriticalsection_, 0x00000400))
        {
            // TODO: handle error
        }

        cancelevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        workevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        loadersemaphore_.SetupSemaphore(kMaximumLoaderCount);
        StartLoading();
    }
    ~ImgLoader()
    {
        StopLoading();
        DeleteCriticalSection(&queuecriticalsection_);
        CloseHandle(cancelevent_);
        CloseHandle(workevent_);
        if (loopthread_ != NULL)
        {
            CloseHandle(loopthread_);
        }
    }
    ImgLoader(const ImgLoader&) = delete;
    ImgLoader& operator=(const ImgLoader&) = delete;
    void LoadAsync(ImgItem* imgitem);
    void LoadNextAsync(ImgItem* imgitem);
    void StopLoading();
private:
    void StartLoading();
    DWORD Loop();
    void QueueItem(ImgItem* imgitem, BOOL pushfront);
    ImgItem* GetNextItem();
    static DWORD WINAPI StaticThreadLoop(void* imgloaderinstance);
    static DWORD WINAPI StaticThreadLoad(void* imgiteminstance);
private:
    std::list<ImgItem*> queue_;
    std::list<std::unique_ptr<LoaderItem>> loaderitems_;
    HANDLE workevent_;
    HANDLE cancelevent_;
    HANDLE loopthread_{ INVALID_HANDLE_VALUE };
    BOOL cancellationflag_{};
    CountingSemaphore loadersemaphore_;
    CRITICAL_SECTION queuecriticalsection_;
};

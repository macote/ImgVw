#pragma once

#include "ImgItem.h"
#include "CountingSemaphore.h"
#include <Windows.h>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class LoaderItem
{
public:
    LoaderItem(ImgItem* imgitem, std::function<void()> handler)
        : imgitem_(imgitem), loadcompleteevent_(handler)
    {
    }
    ImgItem* imgitem() const { return imgitem_; }
    HANDLE loaderitemthread() const { return loaderitemthread_; }
    void set_loaderitemthread(HANDLE loaderitemthread) { loaderitemthread_ = loaderitemthread; }
    void LoadComplete();
private:
    ImgItem* imgitem_{ nullptr };
    HANDLE loaderitemthread_{ INVALID_HANDLE_VALUE };
    std::function<void()> loadcompleteevent_{ nullptr };
};

inline void LoaderItem::LoadComplete()
{
    if (loadcompleteevent_ != nullptr)
    {
        loadcompleteevent_();
    }
}

class ImgLoader
{
public:
    ImgLoader()
    {
        if (!InitializeCriticalSectionAndSpinCount(&queuecriticalsection_, 0x00000400))
        {
            // TODO: handle error
        }

        cancelevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        workevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        loadersemaphore_.set_maximumcount(kMaximumLoaderCount);
        StartLoop();
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
    void LoadAsync(ImgItem* imgitem);
    void LoadNextAsync(ImgItem* imgitem);
    void StopLoading();
private:
    static const INT kMaximumLoaderCount = 2;	// TODO: adjust logic around this limit once GDI+ gets replaced completely
private:
    void StartLoop();
    DWORD Loop();
    BOOL ItemPending();
    void QueueItem(ImgItem* imgitem, BOOL pushfront);
    ImgItem* GetNextItem();
    static DWORD WINAPI StaticThreadLoop(void* imgloaderinstance);
    static DWORD WINAPI StaticThreadLoad(void* imgiteminstance);
private:
    std::deque<ImgItem*> queue_;
    std::deque<std::unique_ptr<LoaderItem>> loaderitems_;
    HANDLE workevent_;
    HANDLE cancelevent_;
    HANDLE loopthread_{ NULL };
    BOOL cancellationflag_{};
    CountingSemaphore loadersemaphore_;
    CRITICAL_SECTION queuecriticalsection_;
};

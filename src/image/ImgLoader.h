#pragma once

#include "ImgItem.h"
#include "CountingSemaphore.h"
#include <Windows.h>
#include <cstddef>
#include <list>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct ImgLoaderStats
{
    std::size_t queued{};
    std::size_t loading{};
    std::size_t free_slots{};
    std::size_t maximum_slots{};
};

class LoaderItem
{
  public:
    LoaderItem(std::shared_ptr<ImgItem> imgitem, std::function<void()> handler)
        : imgitem_(std::move(imgitem)), loadcompleteevent_(handler)
    {
    }
    ~LoaderItem()
    {
        CloseLoaderItemThread();
    }
    LoaderItem(const LoaderItem&) = delete;
    LoaderItem& operator=(const LoaderItem&) = delete;
    std::shared_ptr<ImgItem> imgitem() const
    {
        return imgitem_;
    }
    HANDLE loaderitemthread() const
    {
        return loaderitemthread_;
    }
    void set_loaderitemthread(HANDLE loaderitemthread)
    {
        loaderitemthread_ = loaderitemthread;
    }
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
    std::shared_ptr<ImgItem> imgitem_;
    HANDLE loaderitemthread_{INVALID_HANDLE_VALUE};
    std::function<void()> loadcompleteevent_{nullptr};
};

class ImgLoader
{
  public:
    static constexpr auto kMaximumLoaderCount =
        2; // TODO: adjust logic around this limit once GDI+ gets replaced completely
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
        LoadAsync();
    }
    ~ImgLoader()
    {
        StopLoading();
        DeleteCriticalSection(&queuecriticalsection_);
        if (cancelevent_ != NULL)
        {
            CloseHandle(cancelevent_);
        }
        if (workevent_ != NULL)
        {
            CloseHandle(workevent_);
        }
        if (loopthread_ != NULL && loopthread_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(loopthread_);
        }
    }
    ImgLoader(const ImgLoader&) = delete;
    ImgLoader& operator=(const ImgLoader&) = delete;
    void QueueItem(const std::shared_ptr<ImgItem>& imgitem, BOOL loadnext = FALSE);
    void PrioritizeTargetSize(INT targetwidth, INT targetheight);
    void SetNotificationWindow(HWND hwnd, UINT message);
    void StopLoading();
    ImgLoaderStats GetStats();

  private:
    std::list<std::shared_ptr<ImgItem>> queue_;
    std::set<ImgItem*> pendingitems_;
    std::list<std::unique_ptr<LoaderItem>> loaderitems_;
    HANDLE workevent_;
    HANDLE cancelevent_;
    HANDLE loopthread_{INVALID_HANDLE_VALUE};
    BOOL cancellationflag_{};
    BOOL preferredtargetsizeset_{FALSE};
    INT preferredtargetwidth_{};
    INT preferredtargetheight_{};
    HWND notificationhwnd_{nullptr};
    UINT notificationmessage_{};
    CountingSemaphore loadersemaphore_;
    CRITICAL_SECTION queuecriticalsection_;

  private:
    void LoadAsync();
    DWORD Loop();
    std::shared_ptr<ImgItem> GetNextItem();
    void CompleteItem(const std::shared_ptr<ImgItem>& imgitem, BOOL notifysemaphore);
    void NotifyLoadComplete();
    void CleanupItemThreadObjects();
    static DWORD WINAPI StaticThreadLoop(void* imgloaderinstance);
    static DWORD WINAPI StaticThreadLoad(void* loaderiteminstance);
};

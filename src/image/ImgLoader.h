#pragma once

#include "ImgItem.h"
#include "Win32Handle.h"
#include <Windows.h>
#include <cstddef>
#include <memory>

struct ImgLoaderStats
{
    std::size_t queued{};
    std::size_t loading{};
    std::size_t free_slots{};
    std::size_t maximum_slots{};
};

enum class ImgLoaderStartStatus
{
    Started,
    CriticalSectionFailed,
    CreateWorkEventFailed,
    CreateCancelEventFailed,
    CreateSemaphoreFailed,
    CreateThreadFailed
};

struct ImgLoaderStartResult
{
    ImgLoaderStartStatus status{ImgLoaderStartStatus::CreateThreadFailed};
    DWORD win32_error{ERROR_SUCCESS};

    bool Started() const
    {
        return status == ImgLoaderStartStatus::Started;
    }
};

enum class ImgLoaderStopStatus
{
    AlreadyStopped,
    Stopped,
    SignalFailed,
    TimedOut,
    WaitFailed
};

struct ImgLoaderStopResult
{
    ImgLoaderStopStatus status{ImgLoaderStopStatus::AlreadyStopped};
    DWORD win32_error{ERROR_SUCCESS};

    bool Stopped() const
    {
        return status == ImgLoaderStopStatus::AlreadyStopped || status == ImgLoaderStopStatus::Stopped;
    }
};

class ImgLoader
{
  public:
    static constexpr auto kMaximumLoaderCount =
        2; // TODO: adjust logic around this limit once GDI+ gets replaced completely
    static constexpr auto kCleanupCycleCountTrigger = 29;
    static constexpr DWORD kDefaultStopTimeoutMilliseconds = 3000;

  public:
    ImgLoader();
    ~ImgLoader();
    ImgLoader(const ImgLoader&) = delete;
    ImgLoader& operator=(const ImgLoader&) = delete;
    void QueueItem(const std::shared_ptr<ImgItem>& imgitem, BOOL loadnext = FALSE);
    void PrioritizeTargetSize(INT targetwidth, INT targetheight);
    void SetNotificationWindow(HWND hwnd, UINT message);
    void RemoveNotificationWindow(HWND hwnd);
    void DiscardQueuedItems();
    void DiscardQueuedItemsForTargetSize(INT targetwidth, INT targetheight);
    ImgLoaderStopResult StopLoading(DWORD timeout_milliseconds = kDefaultStopTimeoutMilliseconds);
    ImgLoaderStartResult start_result() const
    {
        return start_result_;
    }
    ImgLoaderStats GetStats();

  private:
    struct State;

    std::shared_ptr<State> state_;
    Win32Handle loopthread_;
    ImgLoaderStartResult start_result_;

  private:
    static DWORD Loop(const std::shared_ptr<State>& state);
    static void CleanupItemThreadObjects(const std::shared_ptr<State>& state);
    static std::shared_ptr<ImgItem> GetNextItem(const std::shared_ptr<State>& state);
    static void CompleteItem(const std::shared_ptr<State>& state, const std::shared_ptr<ImgItem>& imgitem,
                             BOOL notifysemaphore);
    static void NotifyLoadComplete(const std::shared_ptr<State>& state);
    static DWORD WINAPI StaticThreadLoop(void* context);
    static DWORD WINAPI StaticThreadLoad(void* context);
};

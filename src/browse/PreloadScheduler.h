#pragma once

#include "BrowseSession.h"
#include "Win32Handle.h"

#include <Windows.h>

#include <functional>
#include <memory>
#include <vector>

struct PreloadSize
{
    INT width{};
    INT height{};
};

enum class PreloadSchedulerStopStatus
{
    AlreadyStopped,
    Stopped,
    SignalFailed,
    TimedOut,
    WaitFailed
};

struct PreloadSchedulerStopResult
{
    PreloadSchedulerStopStatus status{PreloadSchedulerStopStatus::AlreadyStopped};
    DWORD win32_error{ERROR_SUCCESS};

    bool Stopped() const
    {
        return status == PreloadSchedulerStopStatus::AlreadyStopped || status == PreloadSchedulerStopStatus::Stopped;
    }
};

class PreloadScheduler final
{
  public:
    using Work = std::function<void(const std::shared_ptr<BrowseSessionCancellation>& cancellation)>;

    void Reset();
    bool Queue(Work work);
    PreloadSchedulerStopResult Stop(DWORD timeout_ms);
    void SetActiveTargetSize(INT width, INT height);
    INT targetwidth() const;
    INT targetheight() const;
    bool IsTargetSizeActive(INT width, INT height) const;
    bool AddTargetSize(INT width, INT height);
    bool AddTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<PreloadSize>* added_sizes);
    std::vector<PreloadSize>& target_sizes();
    const std::vector<PreloadSize>& target_sizes() const;

  private:
    struct WorkRequest
    {
        std::shared_ptr<BrowseSessionCancellation> cancellation;
        Work work;
    };

    static DWORD WINAPI StaticThreadQueue(void* context);
    void CleanupCompletedThreads();

    INT targetwidth_{};
    INT targetheight_{};
    std::vector<PreloadSize> target_sizes_;
    std::vector<Win32Handle> threads_;
    std::shared_ptr<BrowseSessionCancellation> cancellation_;
};

#include "PreloadScheduler.h"

#include <algorithm>
#include <memory>

void PreloadScheduler::Reset()
{
    target_sizes_.clear();
}

bool PreloadScheduler::Queue(Work work)
{
    CleanupCompletedThreads();
    if (cancellation_ == nullptr)
    {
        cancellation_ = std::make_shared<BrowseSessionCancellation>();
    }
    if (!cancellation_->valid())
    {
        cancellation_.reset();
        return false;
    }

    auto request = std::make_unique<WorkRequest>();
    request->cancellation = cancellation_;
    request->work = std::move(work);
    const auto thread = CreateThread(nullptr, 0, StaticThreadQueue, request.get(), 0, nullptr);
    if (thread == nullptr)
    {
        return false;
    }

    request.release();
    threads_.emplace_back(thread);
    return true;
}

PreloadSchedulerStopResult PreloadScheduler::Stop(DWORD timeout_ms)
{
    if (threads_.empty())
    {
        cancellation_.reset();
        return {PreloadSchedulerStopStatus::AlreadyStopped, ERROR_SUCCESS};
    }

    if (cancellation_ != nullptr && !cancellation_->Signal())
    {
        return {PreloadSchedulerStopStatus::SignalFailed, GetLastError()};
    }

    const DWORD start = GetTickCount();
    for (const auto& thread : threads_)
    {
        if (!thread.valid())
        {
            continue;
        }

        const DWORD elapsed = GetTickCount() - start;
        const DWORD remaining = elapsed < timeout_ms ? timeout_ms - elapsed : 0;
        const DWORD wait_result = WaitForSingleObject(thread.get(), remaining);
        if (wait_result == WAIT_TIMEOUT)
        {
            return {PreloadSchedulerStopStatus::TimedOut, ERROR_SUCCESS};
        }
        if (wait_result == WAIT_FAILED)
        {
            return {PreloadSchedulerStopStatus::WaitFailed, GetLastError()};
        }
    }

    threads_.clear();
    cancellation_.reset();
    return {PreloadSchedulerStopStatus::Stopped, ERROR_SUCCESS};
}

void PreloadScheduler::SetActiveTargetSize(INT width, INT height)
{
    targetwidth_ = width;
    targetheight_ = height;
}

INT PreloadScheduler::targetwidth() const
{
    return targetwidth_;
}

INT PreloadScheduler::targetheight() const
{
    return targetheight_;
}

bool PreloadScheduler::IsTargetSizeActive(INT width, INT height) const
{
    return (width == targetwidth_ && height == targetheight_) ||
           std::find_if(target_sizes_.begin(), target_sizes_.end(), [width, height](const auto& size) {
               return size.width == width && size.height == height;
           }) != target_sizes_.end();
}

bool PreloadScheduler::AddTargetSize(INT width, INT height)
{
    const auto match = std::find_if(target_sizes_.begin(), target_sizes_.end(), [width, height](const auto& size) {
        return size.width == width && size.height == height;
    });
    if (match != target_sizes_.end())
    {
        return false;
    }

    target_sizes_.push_back({width, height});
    return true;
}

bool PreloadScheduler::AddTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<PreloadSize>* added_sizes)
{
    bool added = false;
    for (const auto& target_size : target_sizes)
    {
        if (target_size.cx <= 0 || target_size.cy <= 0)
        {
            continue;
        }

        if (AddTargetSize(target_size.cx, target_size.cy))
        {
            added = true;
            if (added_sizes != nullptr)
            {
                added_sizes->push_back({target_size.cx, target_size.cy});
            }
        }
    }
    return added;
}

const std::vector<PreloadSize>& PreloadScheduler::target_sizes() const
{
    return target_sizes_;
}

std::vector<PreloadSize>& PreloadScheduler::target_sizes()
{
    return target_sizes_;
}

DWORD WINAPI PreloadScheduler::StaticThreadQueue(void* context)
{
    std::unique_ptr<WorkRequest> request(reinterpret_cast<WorkRequest*>(context));
    if (request != nullptr && request->work)
    {
        request->work(request->cancellation);
    }

    return 0;
}

void PreloadScheduler::CleanupCompletedThreads()
{
    auto thread = threads_.begin();
    while (thread != threads_.end())
    {
        if (!thread->valid() || WaitForSingleObject(thread->get(), 0) == WAIT_OBJECT_0)
        {
            thread = threads_.erase(thread);
        }
        else
        {
            ++thread;
        }
    }
    if (threads_.empty())
    {
        cancellation_.reset();
    }
}

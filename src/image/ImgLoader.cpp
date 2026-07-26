#include "ImgLoader.h"

#include "CountingSemaphore.h"
#include "CriticalSection.h"
#include <algorithm>
#include <list>
#include <set>
#include <utility>
#include <vector>

struct ImgLoaderNotification
{
    HWND hwnd{};
    UINT message{};
};

struct ImgLoader::QueuedItem
{
    std::shared_ptr<ImgItem> imgitem;
    ULONG generation{};
};

struct ImgLoader::State
{
    struct LoaderItem
    {
        LoaderItem(std::shared_ptr<ImgItem> item, ULONG item_generation)
            : imgitem(std::move(item)), generation(item_generation)
        {
        }

        std::shared_ptr<ImgItem> imgitem;
        ULONG generation{};
        Win32Handle thread;
    };

    struct LoopContext
    {
        std::shared_ptr<State> state;
    };

    struct WorkerContext
    {
        std::shared_ptr<State> state;
        std::shared_ptr<LoaderItem> loaderitem;
    };

    State()
    {
        if (!queuecriticalsection.valid())
        {
            initialization_result = {ImgLoaderStartStatus::CriticalSectionFailed, GetLastError()};
            return;
        }

        workevent.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
        if (!workevent.valid())
        {
            initialization_result = {ImgLoaderStartStatus::CreateWorkEventFailed, GetLastError()};
            return;
        }

        cancelevent.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
        if (!cancelevent.valid())
        {
            initialization_result = {ImgLoaderStartStatus::CreateCancelEventFailed, GetLastError()};
            return;
        }

        if (!loadersemaphore.SetupSemaphore(ImgLoader::kMaximumLoaderCount))
        {
            initialization_result = {ImgLoaderStartStatus::CreateSemaphoreFailed, GetLastError()};
            return;
        }

        initialization_result = {ImgLoaderStartStatus::Started, ERROR_SUCCESS};
    }

    CriticalSection queuecriticalsection;
    Win32Handle workevent;
    Win32Handle cancelevent;
    CountingSemaphore loadersemaphore;
    std::list<QueuedItem> queue;
    std::set<ImgItem*> pendingitems;
    std::list<std::shared_ptr<LoaderItem>> loaderitems;
    BOOL preferredtargetsizeset{FALSE};
    INT preferredtargetwidth{};
    INT preferredtargetheight{};
    std::vector<ImgLoaderNotification> notifications;
    bool stopping{};
    ImgLoaderStartResult initialization_result;
};

namespace
{
DWORD RemainingTimeout(DWORD started, DWORD timeout_milliseconds)
{
    if (timeout_milliseconds == INFINITE)
    {
        return INFINITE;
    }

    const auto elapsed = GetTickCount() - started;
    return elapsed >= timeout_milliseconds ? 0 : timeout_milliseconds - elapsed;
}
} // namespace

ImgLoader::ImgLoader() : state_(std::make_shared<State>())
{
    start_result_ = state_->initialization_result;
    if (!start_result_.Started())
    {
        return;
    }

    const auto context = new State::LoopContext{state_};
    loopthread_.reset(CreateThread(nullptr, 0, StaticThreadLoop, context, 0, nullptr));
    if (!loopthread_.valid())
    {
        const auto error = GetLastError();
        delete context;
        start_result_ = {ImgLoaderStartStatus::CreateThreadFailed, error};
    }
}

ImgLoader::~ImgLoader()
{
    StopLoading();
}

ImgLoaderStopResult ImgLoader::StopLoading(DWORD timeout_milliseconds)
{
    if (!loopthread_.valid())
    {
        return {ImgLoaderStopStatus::AlreadyStopped, ERROR_SUCCESS};
    }

    {
        CriticalSectionLock lock(state_->queuecriticalsection);
        state_->stopping = true;
    }

    if (!SetEvent(state_->cancelevent.get()))
    {
        return {ImgLoaderStopStatus::SignalFailed, GetLastError()};
    }

    const auto wait_started = GetTickCount();
    const auto loop_wait = WaitForSingleObject(loopthread_.get(), timeout_milliseconds);
    if (loop_wait == WAIT_TIMEOUT)
    {
        return {ImgLoaderStopStatus::TimedOut, ERROR_TIMEOUT};
    }
    if (loop_wait == WAIT_FAILED)
    {
        return {ImgLoaderStopStatus::WaitFailed, GetLastError()};
    }

    std::vector<HANDLE> worker_threads;
    {
        CriticalSectionLock lock(state_->queuecriticalsection);
        for (const auto& loaderitem : state_->loaderitems)
        {
            if (loaderitem->thread.valid())
            {
                worker_threads.push_back(loaderitem->thread.get());
            }
        }
    }

    for (const auto worker_thread : worker_threads)
    {
        const auto worker_wait =
            WaitForSingleObject(worker_thread, RemainingTimeout(wait_started, timeout_milliseconds));
        if (worker_wait == WAIT_TIMEOUT)
        {
            return {ImgLoaderStopStatus::TimedOut, ERROR_TIMEOUT};
        }
        if (worker_wait == WAIT_FAILED)
        {
            return {ImgLoaderStopStatus::WaitFailed, GetLastError()};
        }
    }

    {
        CriticalSectionLock lock(state_->queuecriticalsection);
        state_->loaderitems.clear();
        state_->queue.clear();
        state_->pendingitems.clear();
    }
    loopthread_.reset();

    return {ImgLoaderStopStatus::Stopped, ERROR_SUCCESS};
}

DWORD ImgLoader::Loop(const std::shared_ptr<State>& state)
{
    const HANDLE wait_events[] = {state->cancelevent.get(), state->workevent.get()};
    INT cyclecount{};

    while (true)
    {
        ++cyclecount;
        const auto wait_result = WaitForMultipleObjects(2, wait_events, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0)
        {
            break;
        }
        if (wait_result != WAIT_OBJECT_0 + 1)
        {
            break;
        }

        auto queueditem = GetNextItem(state);
        if (queueditem.imgitem != nullptr)
        {
            const auto& imgitem = queueditem.imgitem;
            if (imgitem->status() == ImgItem::Status::Queued)
            {
                const auto semaphore_wait = state->loadersemaphore.Wait(state->cancelevent.get());
                if (semaphore_wait == CountingSemaphoreWaitStatus::Cancelled)
                {
                    break;
                }
                if (semaphore_wait == CountingSemaphoreWaitStatus::Failed)
                {
                    CompleteItem(state, imgitem, queueditem.generation, FALSE);
                    break;
                }
                if (WaitForSingleObject(state->cancelevent.get(), 0) == WAIT_OBJECT_0)
                {
                    state->loadersemaphore.Notify();
                    break;
                }

                auto loaderitem = std::make_shared<State::LoaderItem>(imgitem, queueditem.generation);
                const auto context = new State::WorkerContext{state, loaderitem};
                loaderitem->thread.reset(CreateThread(nullptr, 0, StaticThreadLoad, context, 0, nullptr));
                if (!loaderitem->thread.valid())
                {
                    delete context;
                    CompleteItem(state, imgitem, queueditem.generation, TRUE);
                }
                else
                {
                    CriticalSectionLock lock(state->queuecriticalsection);
                    state->loaderitems.push_back(std::move(loaderitem));
                }
            }
            else
            {
                CompleteItem(state, imgitem, queueditem.generation, FALSE);
            }
        }

        if (cyclecount % kCleanupCycleCountTrigger == 0)
        {
            CleanupItemThreadObjects(state);
        }
    }

    return 0;
}

void ImgLoader::CleanupItemThreadObjects(const std::shared_ptr<State>& state)
{
    INT closedthreads{};
    CriticalSectionLock lock(state->queuecriticalsection);
    auto item = state->loaderitems.begin();
    while (item != state->loaderitems.end())
    {
        if (WaitForSingleObject((*item)->thread.get(), 0) == WAIT_OBJECT_0)
        {
            item = state->loaderitems.erase(item);
            ++closedthreads;
        }
        else
        {
            ++item;
        }

        if (closedthreads == kCleanupCycleCountTrigger)
        {
            break;
        }
    }
}

void ImgLoader::QueueItem(const std::shared_ptr<ImgItem>& imgitem, BOOL loadnext, ULONG generation)
{
    if (imgitem == nullptr || !start_result_.Started() || imgitem->status() != ImgItem::Status::Queued)
    {
        return;
    }

    {
        CriticalSectionLock lock(state_->queuecriticalsection);
        if (state_->stopping)
        {
            return;
        }
        if (state_->pendingitems.find(imgitem.get()) != state_->pendingitems.end())
        {
            if (loadnext)
            {
                const auto queueditem =
                    std::find_if(state_->queue.begin(), state_->queue.end(),
                                 [&imgitem](const auto& item) { return item.imgitem.get() == imgitem.get(); });
                if (queueditem != state_->queue.end() && queueditem != state_->queue.begin())
                {
                    state_->queue.splice(state_->queue.begin(), state_->queue, queueditem);
                }
            }
            return;
        }

        state_->pendingitems.insert(imgitem.get());
        if (loadnext)
        {
            state_->queue.push_front({imgitem, generation});
        }
        else if (state_->preferredtargetsizeset && imgitem->targetwidth() == state_->preferredtargetwidth &&
                 imgitem->targetheight() == state_->preferredtargetheight)
        {
            const auto staleitem =
                std::find_if(state_->queue.begin(), state_->queue.end(), [&state = state_](const auto& item) {
                    return item.imgitem->targetwidth() != state->preferredtargetwidth ||
                           item.imgitem->targetheight() != state->preferredtargetheight;
                });
            state_->queue.insert(staleitem, {imgitem, generation});
        }
        else
        {
            state_->queue.push_back({imgitem, generation});
        }
    }

    SetEvent(state_->workevent.get());
}

void ImgLoader::PrioritizeTargetSize(INT targetwidth, INT targetheight)
{
    if (!start_result_.Started())
    {
        return;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    state_->preferredtargetsizeset = TRUE;
    state_->preferredtargetwidth = targetwidth;
    state_->preferredtargetheight = targetheight;

    std::list<QueuedItem> prioritized;
    auto item = state_->queue.begin();
    while (item != state_->queue.end())
    {
        if (item->imgitem->targetwidth() == targetwidth && item->imgitem->targetheight() == targetheight)
        {
            prioritized.splice(prioritized.end(), state_->queue, item++);
        }
        else
        {
            ++item;
        }
    }

    if (!prioritized.empty())
    {
        state_->queue.splice(state_->queue.begin(), prioritized);
    }
}

void ImgLoader::SetNotificationWindow(HWND hwnd, UINT message)
{
    if (!start_result_.Started())
    {
        return;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    const auto existing = std::find_if(state_->notifications.begin(), state_->notifications.end(),
                                       [hwnd](const ImgLoaderNotification& item) { return item.hwnd == hwnd; });
    if (existing != state_->notifications.end())
    {
        existing->message = message;
    }
    else if (hwnd != nullptr && message != 0)
    {
        state_->notifications.push_back({hwnd, message});
    }
}

void ImgLoader::RemoveNotificationWindow(HWND hwnd)
{
    if (!state_ || !state_->queuecriticalsection.valid())
    {
        return;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    state_->notifications.erase(std::remove_if(state_->notifications.begin(), state_->notifications.end(),
                                               [hwnd](const ImgLoaderNotification& item) { return item.hwnd == hwnd; }),
                                state_->notifications.end());
}

void ImgLoader::DiscardQueuedItems()
{
    if (!state_ || !state_->queuecriticalsection.valid())
    {
        return;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    for (const auto& item : state_->queue)
    {
        state_->pendingitems.erase(item.imgitem.get());
    }
    state_->queue.clear();
    ResetEvent(state_->workevent.get());
}

void ImgLoader::DiscardQueuedItemsForTargetSize(INT targetwidth, INT targetheight)
{
    if (!state_ || !state_->queuecriticalsection.valid())
    {
        return;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    auto item = state_->queue.begin();
    while (item != state_->queue.end())
    {
        if (item->imgitem->targetwidth() == targetwidth && item->imgitem->targetheight() == targetheight)
        {
            state_->pendingitems.erase(item->imgitem.get());
            item = state_->queue.erase(item);
        }
        else
        {
            ++item;
        }
    }
    if (state_->queue.empty())
    {
        ResetEvent(state_->workevent.get());
    }
}

ImgLoaderStats ImgLoader::GetStats()
{
    ImgLoaderStats stats;
    stats.maximum_slots = kMaximumLoaderCount;
    if (!state_ || !state_->queuecriticalsection.valid())
    {
        stats.free_slots = stats.maximum_slots;
        return stats;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    stats.queued = state_->queue.size();
    stats.notifications = state_->notifications.size();
    for (const auto& loaderitem : state_->loaderitems)
    {
        const auto status = loaderitem->imgitem->status();
        if (status != ImgItem::Status::Ready && status != ImgItem::Status::Error)
        {
            ++stats.loading;
        }
    }
    stats.free_slots = stats.loading >= stats.maximum_slots ? 0 : stats.maximum_slots - stats.loading;
    return stats;
}

ImgLoader::QueuedItem ImgLoader::GetNextItem(const std::shared_ptr<State>& state)
{
    QueuedItem item;
    CriticalSectionLock lock(state->queuecriticalsection);
    if (!state->queue.empty())
    {
        item = state->queue.front();
        state->queue.pop_front();
    }
    else
    {
        ResetEvent(state->workevent.get());
    }

    return item;
}

void ImgLoader::CompleteItem(const std::shared_ptr<State>& state, const std::shared_ptr<ImgItem>& imgitem,
                             ULONG generation, BOOL notifysemaphore)
{
    {
        CriticalSectionLock lock(state->queuecriticalsection);
        state->pendingitems.erase(imgitem.get());
    }

    if (notifysemaphore)
    {
        state->loadersemaphore.Notify();
    }

    NotifyLoadComplete(state, generation);
}

void ImgLoader::NotifyLoadComplete(const std::shared_ptr<State>& state, ULONG generation)
{
    std::vector<ImgLoaderNotification> notifications;
    {
        CriticalSectionLock lock(state->queuecriticalsection);
        notifications = state->notifications;
    }

    for (const auto& notification : notifications)
    {
        if (notification.hwnd != nullptr && notification.message != 0)
        {
            PostMessage(notification.hwnd, notification.message, generation,
                        static_cast<LPARAM>(ImgNotificationKind::LoadComplete));
        }
    }
}

DWORD WINAPI ImgLoader::StaticThreadLoop(void* context)
{
    std::unique_ptr<State::LoopContext> loop_context(reinterpret_cast<State::LoopContext*>(context));
    return loop_context == nullptr ? 0 : Loop(loop_context->state);
}

DWORD WINAPI ImgLoader::StaticThreadLoad(void* context)
{
    std::unique_ptr<State::WorkerContext> worker_context(reinterpret_cast<State::WorkerContext*>(context));
    if (worker_context == nullptr)
    {
        return 0;
    }

    const auto state = worker_context->state;
    const auto loaderitem = worker_context->loaderitem;
    loaderitem->imgitem->Load();
    CompleteItem(state, loaderitem->imgitem, loaderitem->generation, TRUE);
    return 0;
}

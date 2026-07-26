#include "ImgLoader.h"

#include "CountingSemaphore.h"
#include "CriticalSection.h"
#include <algorithm>
#include <deque>
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

struct ImgLoaderTargetSize
{
    INT width{};
    INT height{};
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

    State(ImgLoader::WorkEventSignal signalworkevent, ImgLoader::NotificationPost postnotification)
        : workeventsignal(signalworkevent == nullptr ? SetEvent : signalworkevent),
          postnotification(postnotification == nullptr ? PostMessage : postnotification)
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
    std::deque<ImgLoaderTargetSize> dispatchtargets;
    ImgLoaderTargetSize lastdispatchedtarget;
    bool haslastdispatchedtarget{};
    std::set<ImgItem*> pendingitems;
    std::list<std::shared_ptr<LoaderItem>> loaderitems;
    std::vector<ImgLoaderNotification> notifications;
    ImgLoader::WorkEventSignal workeventsignal;
    ImgLoader::NotificationPost postnotification;
    std::size_t notificationfailures{};
    DWORD notificationerror{ERROR_SUCCESS};
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

bool SameTargetSize(const ImgLoaderTargetSize& left, const ImgLoaderTargetSize& right)
{
    return left.width == right.width && left.height == right.height;
}

ImgLoaderTargetSize TargetSizeOf(const std::shared_ptr<ImgItem>& item)
{
    return {item->targetwidth(), item->targetheight()};
}

template <typename StateType> void PrioritizeDispatchTarget(StateType& state, const ImgLoaderTargetSize& target)
{
    const auto existing = std::find_if(state.dispatchtargets.begin(), state.dispatchtargets.end(),
                                       [&target](const auto& item) { return SameTargetSize(item, target); });
    if (existing != state.dispatchtargets.end())
    {
        state.dispatchtargets.erase(existing);
    }
    state.dispatchtargets.push_front(target);
}

template <typename StateType> void RegisterDispatchTarget(StateType& state, const ImgLoaderTargetSize& target)
{
    const auto existing = std::find_if(state.dispatchtargets.begin(), state.dispatchtargets.end(),
                                       [&target](const auto& item) { return SameTargetSize(item, target); });
    if (existing != state.dispatchtargets.end())
    {
        return;
    }

    if (state.haslastdispatchedtarget)
    {
        const auto last =
            std::find_if(state.dispatchtargets.begin(), state.dispatchtargets.end(),
                         [&state](const auto& item) { return SameTargetSize(item, state.lastdispatchedtarget); });
        if (last != state.dispatchtargets.end())
        {
            state.dispatchtargets.insert(last, target);
            return;
        }
    }

    state.dispatchtargets.push_back(target);
}

} // namespace

ImgLoader::ImgLoader() : ImgLoader(SetEvent, PostMessage, 0) {}

#if defined(IMGVW_TESTING)
ImgLoader::ImgLoader(WorkEventSignal workeventsignal) : ImgLoader(workeventsignal, PostMessage, 0) {}

ImgLoader::ImgLoader(WorkEventSignal workeventsignal, NotificationPost notificationpost)
    : ImgLoader(workeventsignal, notificationpost, 0)
{
}
#endif

ImgLoader::ImgLoader(WorkEventSignal workeventsignal, NotificationPost notificationpost, int)
    : state_(std::make_shared<State>(workeventsignal, notificationpost))
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
        state_->dispatchtargets.clear();
        state_->haslastdispatchedtarget = false;
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

        const auto semaphore_wait = state->loadersemaphore.Wait(state->cancelevent.get());
        if (semaphore_wait == CountingSemaphoreWaitStatus::Cancelled)
        {
            break;
        }
        if (semaphore_wait == CountingSemaphoreWaitStatus::Failed)
        {
            break;
        }
        if (WaitForSingleObject(state->cancelevent.get(), 0) == WAIT_OBJECT_0)
        {
            state->loadersemaphore.Notify();
            break;
        }

        auto queueditem = GetNextItem(state);
        if (queueditem.imgitem == nullptr)
        {
            state->loadersemaphore.Notify();
            continue;
        }

        const auto& imgitem = queueditem.imgitem;
        if (imgitem->status() != ImgItem::Status::Queued)
        {
            CompleteItem(state, imgitem, queueditem.generation, TRUE);
            continue;
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

ImgLoaderQueueResult ImgLoader::QueueItem(const std::shared_ptr<ImgItem>& imgitem, BOOL loadnext, ULONG generation)
{
    if (imgitem == nullptr || imgitem->status() != ImgItem::Status::Queued)
    {
        return {ImgLoaderQueueStatus::InvalidItem, ERROR_SUCCESS};
    }
    if (!start_result_.Started())
    {
        return {ImgLoaderQueueStatus::LoaderUnavailable, start_result_.win32_error};
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    if (state_->stopping)
    {
        return {ImgLoaderQueueStatus::Stopping, ERROR_SUCCESS};
    }
    if (state_->pendingitems.find(imgitem.get()) != state_->pendingitems.end())
    {
        if (loadnext)
        {
            const auto queueditem =
                std::find_if(state_->queue.begin(), state_->queue.end(),
                             [&imgitem](const auto& item) { return item.imgitem.get() == imgitem.get(); });
            if (queueditem != state_->queue.end())
            {
                if (queueditem != state_->queue.end() && queueditem != state_->queue.begin())
                {
                    state_->queue.splice(state_->queue.begin(), state_->queue, queueditem);
                }
                PrioritizeDispatchTarget(*state_, TargetSizeOf(imgitem));
                return {ImgLoaderQueueStatus::Reprioritized, ERROR_SUCCESS};
            }
        }
        return {ImgLoaderQueueStatus::AlreadyPending, ERROR_SUCCESS};
    }

    const auto target = TargetSizeOf(imgitem);
    const auto previousdispatchtargets = state_->dispatchtargets;
    state_->pendingitems.insert(imgitem.get());
    if (loadnext)
    {
        state_->queue.push_front({imgitem, generation});
        PrioritizeDispatchTarget(*state_, target);
    }
    else
    {
        state_->queue.push_back({imgitem, generation});
        RegisterDispatchTarget(*state_, target);
    }

    if (!state_->workeventsignal(state_->workevent.get()))
    {
        const auto error = GetLastError();
        const auto queueditem = std::find_if(state_->queue.begin(), state_->queue.end(), [&imgitem](const auto& item) {
            return item.imgitem.get() == imgitem.get();
        });
        if (queueditem != state_->queue.end())
        {
            state_->queue.erase(queueditem);
        }
        state_->pendingitems.erase(imgitem.get());
        state_->dispatchtargets = previousdispatchtargets;
        return {ImgLoaderQueueStatus::SignalFailed, error};
    }

    return {ImgLoaderQueueStatus::Queued, ERROR_SUCCESS};
}

void ImgLoader::PrioritizeTargetSize(INT targetwidth, INT targetheight)
{
    if (!start_result_.Started())
    {
        return;
    }

    CriticalSectionLock lock(state_->queuecriticalsection);
    PrioritizeDispatchTarget(*state_, {targetwidth, targetheight});
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
    state_->dispatchtargets.clear();
    state_->haslastdispatchedtarget = false;
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
        state_->dispatchtargets.clear();
        ResetEvent(state_->workevent.get());
    }
    else
    {
        state_->dispatchtargets.erase(std::remove_if(state_->dispatchtargets.begin(), state_->dispatchtargets.end(),
                                                     [targetwidth, targetheight](const auto& target) {
                                                         return target.width == targetwidth &&
                                                                target.height == targetheight;
                                                     }),
                                      state_->dispatchtargets.end());
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
    stats.notification_failures = state_->notificationfailures;
    stats.notification_error = state_->notificationerror;
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
        while (!state->dispatchtargets.empty())
        {
            const auto target = state->dispatchtargets.front();
            state->dispatchtargets.pop_front();
            const auto queueditem =
                std::find_if(state->queue.begin(), state->queue.end(), [&target](const auto& candidate) {
                    return SameTargetSize(TargetSizeOf(candidate.imgitem), target);
                });
            if (queueditem == state->queue.end())
            {
                continue;
            }

            item = *queueditem;
            state->queue.erase(queueditem);
            state->lastdispatchedtarget = target;
            state->haslastdispatchedtarget = true;
            const auto targetstillqueued =
                std::find_if(state->queue.begin(), state->queue.end(), [&target](const auto& candidate) {
                    return SameTargetSize(TargetSizeOf(candidate.imgitem), target);
                });
            if (targetstillqueued != state->queue.end())
            {
                state->dispatchtargets.push_back(target);
            }
            break;
        }

        if (item.imgitem == nullptr)
        {
            item = state->queue.front();
            state->queue.pop_front();
            state->lastdispatchedtarget = TargetSizeOf(item.imgitem);
            state->haslastdispatchedtarget = true;
        }
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
            if (!state->postnotification(notification.hwnd, notification.message, generation,
                                         static_cast<LPARAM>(ImgNotificationKind::LoadComplete)))
            {
                const auto error = GetLastError();
                CriticalSectionLock lock(state->queuecriticalsection);
                ++state->notificationfailures;
                state->notificationerror = error;
            }
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

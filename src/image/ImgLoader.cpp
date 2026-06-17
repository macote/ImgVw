#include "ImgLoader.h"

void ImgLoader::StopLoading()
{
    if (loopthread_ == nullptr || loopthread_ == INVALID_HANDLE_VALUE)
    {
        return;
    }

    cancellationflag_ = TRUE;
    SetEvent(cancelevent_);

    if (WaitForSingleObject(loopthread_, INFINITE) != WAIT_OBJECT_0)
    {
        // TODO: handle error
    }

    if (!loaderitems_.empty())
    {
        std::vector<HANDLE> threads;
        for (const auto& loaderitem : loaderitems_)
        {
            const auto status = loaderitem->imgitem()->status();
            if (!(status == ImgItem::Status::Error || status == ImgItem::Status::Ready))
            {
                threads.push_back(loaderitem->loaderitemthread());
            }
        }

        if (!threads.empty())
        {
            if (WaitForMultipleObjects(threads.size(), &threads[0], TRUE, INFINITE) == WAIT_FAILED)
            {
                // TODO: handle error
            }
        }

        loaderitems_.clear();
    }

    cancellationflag_ = FALSE;
}

void ImgLoader::LoadAsync()
{
    loopthread_ = CreateThread(nullptr, 0, StaticThreadLoop, reinterpret_cast<void*>(this), 0, nullptr);
}

DWORD ImgLoader::Loop()
{
    const HANDLE waitevents[2] = {workevent_, cancelevent_};
    INT cyclecount{};

    while (!cancellationflag_)
    {
        ++cyclecount;
        if (WaitForMultipleObjects(2, waitevents, FALSE, INFINITE) == WAIT_FAILED)
        {
            // TODO: handle error
            break;
        }

        if (cancellationflag_)
        {
            break;
        }

        auto imgitem = GetNextItem();
        if (imgitem != nullptr)
        {
            loadersemaphore_.Wait();
            if (imgitem->status() == ImgItem::Status::Queued)
            {
                auto loaderitem = std::make_unique<LoaderItem>(imgitem, [&]() { loadersemaphore_.Notify(); });
                loaderitem->set_loaderitemthread(
                    CreateThread(nullptr, 0, StaticThreadLoad, reinterpret_cast<void*>(loaderitem.get()), 0, nullptr));
                loaderitems_.push_back(std::move(loaderitem));
            }
            else
            {
                loadersemaphore_.Notify();
            }
        }

        if (!loaderitems_.empty() && cyclecount % kCleanupCycleCountTrigger == 0)
        {
            CleanupItemThreadObjects();
        }
    }

    return 0;
}

void ImgLoader::CleanupItemThreadObjects()
{
    INT closedthreads{};
    auto it = loaderitems_.begin();
    while (it != loaderitems_.end())
    {
        if (WaitForSingleObject((*it).get()->loaderitemthread(), 0) == WAIT_OBJECT_0)
        {
            (*it).get()->CloseLoaderItemThread();
            ++closedthreads;
            loaderitems_.erase(it++);
        }
        else
        {
            it++;
        }

        if (closedthreads == kCleanupCycleCountTrigger)
        {
            break;
        }
    }
}

void ImgLoader::QueueItem(ImgItem* imgitem, BOOL loadnext)
{
    EnterCriticalSection(&queuecriticalsection_);
    if (loadnext)
    {
        queue_.push_front(imgitem);
    }
    else
    {
        queue_.push_back(imgitem);
    }

    LeaveCriticalSection(&queuecriticalsection_);

    SetEvent(workevent_);
}

ImgItem* ImgLoader::GetNextItem()
{
    ImgItem* imgitem = nullptr;
    EnterCriticalSection(&queuecriticalsection_);
    if (!queue_.empty())
    {
        imgitem = queue_.front();
        queue_.pop_front();
    }
    else
    {
        ResetEvent(workevent_);
    }

    LeaveCriticalSection(&queuecriticalsection_);

    return imgitem;
}

DWORD WINAPI ImgLoader::StaticThreadLoop(void* imgloaderinstance)
{
    auto imgloader = reinterpret_cast<ImgLoader*>(imgloaderinstance);
    imgloader->Loop();

    return 0;
}

DWORD WINAPI ImgLoader::StaticThreadLoad(void* loaderiteminstance)
{
    auto loaderitem = reinterpret_cast<LoaderItem*>(loaderiteminstance);
    loaderitem->imgitem()->Load();
    loaderitem->LoadComplete();

    return 0;
}

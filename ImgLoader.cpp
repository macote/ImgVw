#include "ImgLoader.h"

void ImgLoader::LoadAsync(ImgItem* imgitem)
{
    QueueItem(imgitem, FALSE);
}

void ImgLoader::LoadNextAsync(ImgItem* imgitem)
{
    QueueItem(imgitem, TRUE);
}

void ImgLoader::StopLoading()
{
    cancellationflag_ = TRUE;
    SetEvent(cancelevent_);

    if (WaitForSingleObject(loopthread_, INFINITE) != WAIT_OBJECT_0)
    {
        // TODO: handle error
    }

    if (loaderitems_.size() > 0)
    {
        std::vector<HANDLE> threads;
        for (const auto & loaderitem : loaderitems_)
        {
            const auto status = loaderitem->imgitem()->status();
            if (!(status == ImgItem::Status::Error || status == ImgItem::Status::Ready))
            {
                threads.push_back(loaderitem->loaderitemthread());
            }
        }

        if (threads.size() > 0)
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

void ImgLoader::StartLoading()
{
    loopthread_ = CreateThread(NULL, 0, StaticThreadLoop, reinterpret_cast<void*>(this), 0, NULL);
}

DWORD ImgLoader::Loop()
{
    const HANDLE waitevents[2] = { workevent_, cancelevent_ };
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
                loaderitem->set_loaderitemthread(CreateThread(NULL, 0, StaticThreadLoad, reinterpret_cast<void*>(loaderitem.get()), 0, NULL));
                loaderitems_.push_back(std::move(loaderitem));
            }
            else
            {
                loadersemaphore_.Notify();
            }
        }

        if (loaderitems_.size() > 0 && cyclecount % kCleanupCycleCountTrigger == 0)
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
    }

    return 0;
}

void ImgLoader::QueueItem(ImgItem* imgitem, BOOL pushfront)
{
    EnterCriticalSection(&queuecriticalsection_);
    if (pushfront)
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
    if (queue_.size() > 0)
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

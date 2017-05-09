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
        for (auto it = loaderitems_.begin(); it != loaderitems_.end(); ++it)
        {
            threads.push_back((*it)->loaderitemthread());
        }

        if (WaitForMultipleObjects(threads.size(), &threads[0], TRUE, INFINITE) == WAIT_FAILED)
        {
            // TODO: handle error
        }

        loaderitems_.clear();
    }

    cancellationflag_ = FALSE;
}

void ImgLoader::StartLoop()
{
    loopthread_ = CreateThread(NULL, 0, StaticThreadLoop, (void*)this, 0, NULL);
}

DWORD ImgLoader::Loop()
{
    HANDLE events[2] = { workevent_, cancelevent_ };

    while (!cancellationflag_)
    {
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_FAILED)
        {
            // TODO: handle error
            break;
        }

        if (cancellationflag_)
        {
            break;
        }

        if (ItemPending())
        {
            loadersemaphore_.Wait();
            auto imgitem = GetNextItem();
            if (imgitem != NULL && imgitem->status() == ImgItem::Status::Queued)
            {
                auto loaderitem = std::make_unique<LoaderItem>(imgitem, [this]() { loadersemaphore_.Notify(); });
                loaderitem->set_loaderitemthread(CreateThread(NULL, 0, StaticThreadLoad, (void*)loaderitem.get(), 0, NULL));
                loaderitems_.push_back(std::move(loaderitem));
            }
            else
            {
                loadersemaphore_.Notify();
            }
        }
    }

    return 0;
}

BOOL ImgLoader::ItemPending()
{
    return queue_.size() > 0;
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
    ImgItem* imgitem = NULL;
    EnterCriticalSection(&queuecriticalsection_);
    if (queue_.size() > 0)
    {
        imgitem = queue_.front();
        queue_.pop_front();
    }

    if (queue_.size() == 0)
    {
        ResetEvent(workevent_);
    }

    LeaveCriticalSection(&queuecriticalsection_);
    return imgitem;
}

DWORD WINAPI ImgLoader::StaticThreadLoop(void* imgloaderinstance)
{
    auto imgloader = (ImgLoader*)imgloaderinstance;
    imgloader->Loop();
    return 0;
}

DWORD WINAPI ImgLoader::StaticThreadLoad(void* loaderiteminstance)
{
    auto loaderitem = (LoaderItem*)loaderiteminstance;
    loaderitem->imgitem()->Load();
    loaderitem->LoadComplete();
    return 0;
}
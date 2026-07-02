#include "ImgBrowser.h"
#include <Shlwapi.h>
#include <algorithm>

void ImgBrowser::CollectFile(const std::wstring& filepath, ImgItem::Format imgformat)
{
    EnterCriticalSection(&browsecriticalsection_);

    if (files_.Add(filepath))
    {
        QueueFileForTargetSizes(filepath, imgformat, FALSE);
    }

    SetEvent(readyevent_);
    NotifyChanged();

    LeaveCriticalSection(&browsecriticalsection_);
}

void ImgBrowser::CollectFolder(const std::wstring& folderpath)
{
    WIN32_FIND_DATA findfiledata;
    HANDLE hFind;
    std::wstring pattern = folderpath + L"*";
    hFind = FindFirstFile(pattern.c_str(), &findfiledata);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (lstrcmp(findfiledata.cFileName, L".") != 0 && lstrcmp(findfiledata.cFileName, L"..") != 0)
                {
                    std::wstring currentpath(folderpath + findfiledata.cFileName + L"\\");
                    if (recursive_)
                    {
                        CollectFolder(currentpath);
                    }
                    else
                    {
                        folders_.push_back(currentpath);
                    }
                }
            }
            else
            {
                std::wstring currentfile(folderpath + findfiledata.cFileName);
                const auto imgformat = ResolveFileFormat(currentfile);
                if (imgformat != ImgItem::Format::Unsupported)
                {
                    CollectFile(currentfile, imgformat);
                }
            }
        } while (FindNextFile(hFind, &findfiledata) && !cancellationflag_);

        FindClose(hFind);
    }
}

ImgItem::Format ImgBrowser::ResolveFileFormat(const std::wstring& filepath)
{
    return ImgItemFactory::ResolveFormat(filepath);
}

void ImgBrowser::BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight)
{
    StopTargetQueueing();
    Reset();

    targetwidth_ = targetwidth;
    targetheight_ = targetheight;
    AddTargetSize(targetwidth_, targetheight_);
    loader_.PrioritizeTargetSize(targetwidth_, targetheight_);

    WIN32_FIND_DATA findfiledata{};
    HANDLE findfilehandle{};
    BOOL forcedfolder{};
    std::wstring workpath = path;

    if (workpath.empty())
    {
        const INT BUFFERSIZE = 4096;
        TCHAR currentdirectory[BUFFERSIZE];
        GetCurrentDirectory(BUFFERSIZE, currentdirectory);
        workpath = currentdirectory;
    }

    if (workpath.back() == L'\\')
    {
        workpath = workpath.substr(0, workpath.size() - 1);
        forcedfolder = TRUE;
    }

    findfilehandle = FindFirstFile(workpath.c_str(), &findfiledata);
    if (findfilehandle != INVALID_HANDLE_VALUE)
    {
        FindClose(findfilehandle);
        if (!(findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !forcedfolder)
        {
            const auto backslashposition = workpath.rfind(L'\\');
            if (backslashposition != std::wstring::npos)
            {
                folderpath_ = workpath.substr(0, backslashposition + 1);
            }
            else
            {
                folderpath_ = L".\\";
                workpath = folderpath_ + workpath;
            }

            const auto imgformat = ResolveFileFormat(workpath);
            if (imgformat != ImgItem::Format::Unsupported)
            {
                CollectFile(workpath, imgformat);
            }
        }
        else
        {
            folderpath_ = workpath + L"\\";
        }
    }
    else
    {
        TCHAR mypicturespath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, mypicturespath)))
        {
            folderpath_ = std::wstring(mypicturespath) + L"\\";
        }
        else
        {
            // TODO: handle error
        }
    }

    ResetEvent(readyevent_);

    collectorthread_ = CreateThread(nullptr, 0, StaticThreadCollect, reinterpret_cast<void*>(this), 0, nullptr);
}

BOOL ImgBrowser::UpdateTargetSize(INT targetwidth, INT targetheight)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return FALSE;
    }

    EnterCriticalSection(&browsecriticalsection_);
    const auto changed = targetwidth_ != targetwidth || targetheight_ != targetheight;
    if (changed)
    {
        targetwidth_ = targetwidth;
        targetheight_ = targetheight;
        AddTargetSize(targetwidth_, targetheight_);
    }

    LeaveCriticalSection(&browsecriticalsection_);
    if (changed)
    {
        loader_.PrioritizeTargetSize(targetwidth, targetheight);
        QueueTargetSizeAsync(targetwidth, targetheight, TRUE);
    }

    return changed ? TRUE : FALSE;
}

void ImgBrowser::BrowseSubFoldersAsync()
{
    if (recursive_)
    {
        return;
    }

    recursive_ = TRUE;

    const auto collectorstatus = WaitForSingleObject(collectorthread_, 0);
    if (collectorstatus == WAIT_TIMEOUT)
    {
        return;
    }

    if (collectorstatus != WAIT_OBJECT_0)
    {
        // TODO: handle error
        return;
    }

    if (!cancellationflag_ && !folders_.empty())
    {
        ResetEvent(readyevent_);

        CloseHandle(collectorthread_);
        collectorthread_ =
            CreateThread(nullptr, 0, StaticThreadCollectSubFolders, reinterpret_cast<void*>(this), 0, nullptr);
    }
}

void ImgBrowser::StopCollecting()
{
    if (collectorthread_ == nullptr || collectorthread_ == INVALID_HANDLE_VALUE)
    {
        return;
    }

    cancellationflag_ = TRUE;

    const DWORD timeoutMs = 3000;
    const DWORD waitResult = WaitForSingleObject(collectorthread_, timeoutMs);
    if (waitResult == WAIT_TIMEOUT)
    {
#if defined(IMGVW_DEBUG)
        OutputDebugString(L"ImgBrowser::StopCollecting: Warning: collector thread did not terminate within timeout.\n");
#endif
    }
    else if (waitResult == WAIT_FAILED)
    {
#if defined(IMGVW_DEBUG)
        const DWORD error = GetLastError();
        WCHAR buf[256];
        swprintf_s(buf, L"ImgBrowser::StopCollecting: WaitForSingleObject failed with error 0x%08lX\n",
                   static_cast<unsigned long>(error));
        OutputDebugString(buf);
#endif
    }

    cancellationflag_ = FALSE;
}

void ImgBrowser::StopBrowsing()
{
    StopCollecting();
    StopTargetQueueing();
    if (collectorthread_ != nullptr)
    {
        CloseHandle(collectorthread_);
        collectorthread_ = nullptr;
    }

    loader_.StopLoading();
}

void ImgBrowser::StopTargetQueueing()
{
    if (targetqueuethreads_.empty())
    {
        return;
    }

    cancellationflag_ = TRUE;
    for (const auto thread : targetqueuethreads_)
    {
        if (thread == nullptr || thread == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        const DWORD timeoutMs = 3000;
        const DWORD waitResult = WaitForSingleObject(thread, timeoutMs);
        if (waitResult == WAIT_TIMEOUT)
        {
#if defined(IMGVW_DEBUG)
            OutputDebugString(L"ImgBrowser::StopTargetQueueing: Warning: target queue thread did not terminate.\n");
#endif
        }
        else if (waitResult == WAIT_FAILED)
        {
#if defined(IMGVW_DEBUG)
            const DWORD error = GetLastError();
            WCHAR buf[256];
            swprintf_s(buf, L"ImgBrowser::StopTargetQueueing: WaitForSingleObject failed with error 0x%08lX\n",
                       static_cast<unsigned long>(error));
            OutputDebugString(buf);
#endif
        }

        CloseHandle(thread);
    }

    targetqueuethreads_.clear();
    cancellationflag_ = FALSE;
}

void ImgBrowser::SetNotificationWindow(HWND hwnd, UINT message)
{
    notificationhwnd_ = hwnd;
    notificationmessage_ = message;
    loader_.SetNotificationWindow(hwnd, message);
}

void ImgBrowser::Reset()
{
    files_.Clear();
    target_sizes_.clear();
}

BOOL ImgBrowser::AddTargetSize(INT targetwidth, INT targetheight)
{
    const auto match = std::find_if(target_sizes_.begin(), target_sizes_.end(),
                                    [targetwidth, targetheight](const TargetSize& target_size) {
                                        return target_size.width == targetwidth && target_size.height == targetheight;
                                    });
    if (match != target_sizes_.end())
    {
        return FALSE;
    }

    target_sizes_.push_back({targetwidth, targetheight});
    return TRUE;
}

BOOL ImgBrowser::AddTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes)
{
    BOOL added = FALSE;
    for (const auto& target_size : target_sizes)
    {
        if (target_size.cx <= 0 || target_size.cy <= 0)
        {
            continue;
        }

        if (AddTargetSize(target_size.cx, target_size.cy))
        {
            added = TRUE;
            if (added_sizes != nullptr)
            {
                added_sizes->push_back({target_size.cx, target_size.cy});
            }
        }
    }

    return added;
}

void ImgBrowser::QueueTargetSizes(const std::vector<TargetSize>& target_sizes, BOOL loadnext)
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto paths = files_.PathsFromCurrent();
    LeaveCriticalSection(&browsecriticalsection_);

    BOOL next = loadnext;
    for (const auto& filepath : paths)
    {
        if (cancellationflag_)
        {
            break;
        }

        const auto imgformat = ResolveFileFormat(filepath);
        if (imgformat == ImgItem::Format::Unsupported)
        {
            continue;
        }

        for (const auto& target_size : target_sizes)
        {
            if (cancellationflag_)
            {
                break;
            }

            EnterCriticalSection(&browsecriticalsection_);
            const auto imgitem = GetOrCreateCachedItem(filepath, target_size.width, target_size.height, imgformat);
            LeaveCriticalSection(&browsecriticalsection_);
            if (imgitem != nullptr)
            {
                loader_.QueueItem(imgitem, next);
                next = FALSE;
            }
        }
    }
}

void ImgBrowser::QueueTargetSizeAsync(INT targetwidth, INT targetheight, BOOL loadnext)
{
    QueueTargetSizesAsync(std::vector<TargetSize>{{targetwidth, targetheight}}, loadnext);
}

void ImgBrowser::QueueTargetSizesAsync(const std::vector<TargetSize>& target_sizes, BOOL loadnext)
{
    CleanupTargetQueueThreads();

    const auto request = new TargetSizeQueueRequest{this, target_sizes, loadnext};
    const auto thread = CreateThread(nullptr, 0, StaticThreadQueueTargetSize, request, 0, nullptr);
    if (thread == nullptr)
    {
        delete request;
        return;
    }

    targetqueuethreads_.push_back(thread);
}

void ImgBrowser::QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext)
{
    BOOL next = loadnext;
    for (const auto& target_size : target_sizes_)
    {
        const auto imgitem = GetOrCreateCachedItem(filepath, target_size.width, target_size.height, imgformat);
        if (imgitem != nullptr)
        {
            loader_.QueueItem(imgitem, next);
            next = FALSE;
        }
    }
}

void ImgBrowser::CleanupTargetQueueThreads()
{
    auto thread = targetqueuethreads_.begin();
    while (thread != targetqueuethreads_.end())
    {
        if (*thread == nullptr || *thread == INVALID_HANDLE_VALUE || WaitForSingleObject(*thread, 0) == WAIT_OBJECT_0)
        {
            if (*thread != nullptr && *thread != INVALID_HANDLE_VALUE)
            {
                CloseHandle(*thread);
            }
            thread = targetqueuethreads_.erase(thread);
        }
        else
        {
            ++thread;
        }
    }
}

std::wstring ImgBrowser::GetCurrentFilePath()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    LeaveCriticalSection(&browsecriticalsection_);
    return filepath;
}

std::shared_ptr<ImgItem> ImgBrowser::GetCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    const auto imgitem = filepath.empty() ? std::shared_ptr<ImgItem>() : GetOrCreateCachedItem(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    if (imgitem != nullptr)
    {
        if (imgitem->status() == ImgItem::Status::Queued)
        {
            loader_.QueueItem(imgitem, TRUE);
        }

        return imgitem;
    }

    return {};
}

void ImgBrowser::ReloadCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    const auto imgitem = filepath.empty() ? std::shared_ptr<ImgItem>() : GetOrCreateCachedItem(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    if (imgitem != nullptr)
    {
        if (imgitem->status() != ImgItem::Status::Queued)
        {
            imgitem->Unload();
        }

        loader_.QueueItem(imgitem, TRUE);
    }
}

BOOL ImgBrowser::PreloadTargetSize(INT targetwidth, INT targetheight)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return FALSE;
    }

    EnterCriticalSection(&browsecriticalsection_);
    const auto added = AddTargetSize(targetwidth, targetheight);
    LeaveCriticalSection(&browsecriticalsection_);
    if (added)
    {
        QueueTargetSizeAsync(targetwidth, targetheight, FALSE);
    }

    return added;
}

BOOL ImgBrowser::PreloadTargetSizes(const std::vector<SIZE>& target_sizes)
{
    std::vector<TargetSize> added_sizes;
    EnterCriticalSection(&browsecriticalsection_);
    const auto added = AddTargetSizes(target_sizes, &added_sizes);
    LeaveCriticalSection(&browsecriticalsection_);
    if (added)
    {
        QueueTargetSizesAsync(added_sizes, FALSE);
    }

    return added;
}

ImgBrowserStats ImgBrowser::GetStats()
{
    ImgBrowserStats stats;
    EnterCriticalSection(&browsecriticalsection_);
    stats.found_images = files_.Size();
    stats.targetwidth = targetwidth_;
    stats.targetheight = targetheight_;
    stats.sizes = cache_.GetSizeStats();
    LeaveCriticalSection(&browsecriticalsection_);
    stats.loader = loader_.GetStats();

    return stats;
}

BOOL ImgBrowser::MoveToNext()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToNext();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToPrevious()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToPrevious();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToFirst()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToFirst();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToLast()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToLast();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToItem(const std::wstring& filepath)
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveTo(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToOrAddItem(const std::wstring& filepath)
{
    const auto imgformat = ResolveFileFormat(filepath);
    if (imgformat == ImgItem::Format::Unsupported)
    {
        return FALSE;
    }

    EnterCriticalSection(&browsecriticalsection_);
    if (files_.Add(filepath))
    {
        QueueFileForTargetSizes(filepath, imgformat, FALSE);
    }

    const auto moveSuccess = files_.MoveTo(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    if (moveSuccess)
    {
        NotifyChanged();
    }

    return moveSuccess;
}

BOOL ImgBrowser::MoveToRandom()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToRandom();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

void ImgBrowser::RemoveCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    if (!filepath.empty())
    {
        cache_.Remove(filepath);
        files_.RemoveCurrent();
    }

    LeaveCriticalSection(&browsecriticalsection_);
}

void ImgBrowser::CollectSubFolders()
{
    for (const auto& folder : folders_)
    {
        if (cancellationflag_)
        {
            break;
        }
        CollectFolder(folder);
    }

    folders_.clear();
}

std::shared_ptr<ImgItem> ImgBrowser::GetOrCreateCachedItem(const std::wstring& filepath)
{
    const auto imgformat = ResolveFileFormat(filepath);
    return GetOrCreateCachedItem(filepath, targetwidth_, targetheight_, imgformat);
}

std::shared_ptr<ImgItem> ImgBrowser::GetOrCreateCachedItem(const std::wstring& filepath, INT targetwidth,
                                                           INT targetheight, ImgItem::Format imgformat)
{
    if (imgformat == ImgItem::Format::Unsupported)
    {
        return {};
    }

    auto imgitem = cache_.Get(filepath, targetwidth, targetheight);
    if (imgitem == nullptr)
    {
        imgitem = cache_.Add(filepath, targetwidth, targetheight, imgformat);
    }

    return imgitem;
}

void ImgBrowser::NotifyChanged()
{
    if (notificationhwnd_ != nullptr && notificationmessage_ != 0)
    {
        PostMessage(notificationhwnd_, notificationmessage_, 0, 0);
    }
}

DWORD WINAPI ImgBrowser::StaticThreadCollect(void* browserinstance)
{
    ImgBrowser* browser = reinterpret_cast<ImgBrowser*>(browserinstance);

    browser->CollectFolder(browser->folderpath_);
    if (browser->recursive_)
    {
        browser->CollectSubFolders();
    }

    SetEvent(browser->readyevent_);
    browser->NotifyChanged();

    return 0;
}

DWORD WINAPI ImgBrowser::StaticThreadCollectSubFolders(void* browserinstance)
{
    ImgBrowser* browser = reinterpret_cast<ImgBrowser*>(browserinstance);

    browser->CollectSubFolders();

    SetEvent(browser->readyevent_);
    browser->NotifyChanged();

    return 0;
}

DWORD WINAPI ImgBrowser::StaticThreadQueueTargetSize(void* targetsizequeuerequest)
{
    const auto request = reinterpret_cast<TargetSizeQueueRequest*>(targetsizequeuerequest);
    if (request != nullptr && request->browser != nullptr)
    {
        request->browser->QueueTargetSizes(request->sizes, request->loadnext);
    }

    delete request;
    return 0;
}

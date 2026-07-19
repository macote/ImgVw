#include "ImgBrowser.h"
#include "ImageFormatResolver.h"

#include <Shlwapi.h>
#include <algorithm>
#include <utility>

void ImgBrowser::ShareLoadContext(const std::shared_ptr<ImgBrowserLoadContext>& context)
{
    if (context == nullptr || loadcontext_ == context)
    {
        return;
    }

    EnterCriticalSection(&browsecriticalsection_);
    if (notificationhwnd_ != nullptr)
    {
        loadcontext_->loader->RemoveNotificationWindow(notificationhwnd_);
    }
    loadcontext_ = context;
    if (notificationhwnd_ != nullptr && notificationmessage_ != 0)
    {
        loadcontext_->loader->SetNotificationWindow(notificationhwnd_, notificationmessage_);
    }
    LeaveCriticalSection(&browsecriticalsection_);
}

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
    return ImageFormatResolver::Resolve(filepath);
}

BOOL ImgBrowser::BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight, BOOL clearloadcontext)
{
    WIN32_FIND_DATA findfiledata{};
    HANDLE findfilehandle{};
    BOOL forcedfolder{};
    std::wstring workpath = path;

    if (workpath.empty())
    {
        return FALSE;
    }

    if (workpath.back() == L'\\')
    {
        workpath = workpath.substr(0, workpath.size() - 1);
        forcedfolder = TRUE;
    }

    findfilehandle = FindFirstFile(workpath.c_str(), &findfiledata);
    if (findfilehandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    FindClose(findfilehandle);
    if (!StopCollecting() || !StopTargetQueueing())
    {
        return FALSE;
    }

    if (clearloadcontext)
    {
        loadcontext_->Clear();
    }

    Reset();

    targetwidth_ = targetwidth;
    targetheight_ = targetheight;
    loadcontext_->loader->PrioritizeTargetSize(targetwidth_, targetheight_);

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
            MoveToItem(workpath);
        }
    }
    else
    {
        folderpath_ = workpath + L"\\";
    }

    ResetEvent(readyevent_);

    collectorthread_ = CreateThread(nullptr, 0, StaticThreadCollect, reinterpret_cast<void*>(this), 0, nullptr);
    return TRUE;
}

BOOL ImgBrowser::UpdateTargetSize(INT targetwidth, INT targetheight)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return FALSE;
    }

    EnterCriticalSection(&browsecriticalsection_);
    const auto changed = targetwidth_ != targetwidth || targetheight_ != targetheight;
    const auto previouswidth = targetwidth_;
    const auto previousheight = targetheight_;
    BOOL keep_previous_target = FALSE;
    if (changed)
    {
        targetwidth_ = targetwidth;
        targetheight_ = targetheight;
        keep_previous_target = IsTargetSizeActiveLocked(previouswidth, previousheight);
    }

    LeaveCriticalSection(&browsecriticalsection_);
    if (changed)
    {
        if (!keep_previous_target)
        {
            loadcontext_->loader->DiscardQueuedItemsForTargetSize(previouswidth, previousheight);
        }
        loadcontext_->loader->PrioritizeTargetSize(targetwidth, targetheight);
        QueueTargetSizeAsync(targetwidth, targetheight, TRUE);
    }

    return changed ? TRUE : FALSE;
}

BOOL ImgBrowser::BrowseSubFoldersAsync()
{
    if (recursive_)
    {
        return FALSE;
    }

    recursive_ = TRUE;

    const auto collectorstatus = WaitForSingleObject(collectorthread_, 0);
    if (collectorstatus == WAIT_TIMEOUT)
    {
        return TRUE;
    }

    if (collectorstatus != WAIT_OBJECT_0)
    {
        // TODO: handle error
        return FALSE;
    }

    if (!cancellationflag_ && !folders_.empty())
    {
        ResetEvent(readyevent_);

        CloseHandle(collectorthread_);
        collectorthread_ =
            CreateThread(nullptr, 0, StaticThreadCollectSubFolders, reinterpret_cast<void*>(this), 0, nullptr);
        return collectorthread_ != nullptr ? TRUE : FALSE;
    }

    return FALSE;
}

BOOL ImgBrowser::IsCollectingComplete() const
{
    if (collectorthread_ == nullptr || collectorthread_ == INVALID_HANDLE_VALUE)
    {
        return TRUE;
    }

    return WaitForSingleObject(collectorthread_, 0) == WAIT_OBJECT_0 ? TRUE : FALSE;
}

BOOL ImgBrowser::StopCollecting()
{
    if (collectorthread_ == nullptr || collectorthread_ == INVALID_HANDLE_VALUE)
    {
        return TRUE;
    }

    cancellationflag_ = TRUE;

    const DWORD timeoutMs = 3000;
    const DWORD waitResult = WaitForSingleObject(collectorthread_, timeoutMs);
    if (waitResult == WAIT_TIMEOUT)
    {
#if defined(IMGVW_DEBUG)
        OutputDebugString(L"ImgBrowser::StopCollecting: Warning: collector thread did not terminate within timeout.\n");
#endif
        return FALSE;
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
        return FALSE;
    }

    CloseHandle(collectorthread_);
    collectorthread_ = nullptr;
    cancellationflag_ = FALSE;
    return TRUE;
}

void ImgBrowser::StopBrowsing()
{
    StopCollecting();
    StopTargetQueueing();
    if (notificationhwnd_ != nullptr)
    {
        loadcontext_->loader->RemoveNotificationWindow(notificationhwnd_);
        notificationhwnd_ = nullptr;
        notificationmessage_ = 0;
    }
}

BOOL ImgBrowser::StopTargetQueueing()
{
    if (targetqueuethreads_.empty())
    {
        return TRUE;
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
            return FALSE;
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
            return FALSE;
        }

        CloseHandle(thread);
    }

    targetqueuethreads_.clear();
    cancellationflag_ = FALSE;
    return TRUE;
}

void ImgBrowser::SetNotificationWindow(HWND hwnd, UINT message)
{
    if (notificationhwnd_ != nullptr && notificationhwnd_ != hwnd)
    {
        loadcontext_->loader->RemoveNotificationWindow(notificationhwnd_);
    }
    notificationhwnd_ = hwnd;
    notificationmessage_ = message;
    loadcontext_->loader->SetNotificationWindow(hwnd, message);
}

void ImgBrowser::Reset()
{
    files_.Clear();
    folderpath_.clear();
    folders_.clear();
    recursive_ = FALSE;
    preload_target_sizes_.clear();
}

BOOL ImgBrowser::AddPreloadTargetSize(INT targetwidth, INT targetheight)
{
    const auto match = std::find_if(preload_target_sizes_.begin(), preload_target_sizes_.end(),
                                    [targetwidth, targetheight](const TargetSize& target_size) {
                                        return target_size.width == targetwidth && target_size.height == targetheight;
                                    });
    if (match != preload_target_sizes_.end())
    {
        return FALSE;
    }

    preload_target_sizes_.push_back({targetwidth, targetheight});
    return TRUE;
}

BOOL ImgBrowser::AddPreloadTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes)
{
    BOOL added = FALSE;
    for (const auto& target_size : target_sizes)
    {
        if (target_size.cx <= 0 || target_size.cy <= 0)
        {
            continue;
        }

        if (AddPreloadTargetSize(target_size.cx, target_size.cy))
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

BOOL ImgBrowser::IsTargetSizeActiveLocked(INT targetwidth, INT targetheight) const
{
    if (targetwidth_ == targetwidth && targetheight_ == targetheight)
    {
        return TRUE;
    }

    return std::find_if(preload_target_sizes_.begin(), preload_target_sizes_.end(),
                        [targetwidth, targetheight](const TargetSize& target_size) {
                            return target_size.width == targetwidth && target_size.height == targetheight;
                        }) != preload_target_sizes_.end();
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
            const auto active = IsTargetSizeActiveLocked(target_size.width, target_size.height);
            const auto imgitem = active
                                     ? GetOrCreateCachedItem(filepath, target_size.width, target_size.height, imgformat)
                                     : std::shared_ptr<ImgItem>();
            LeaveCriticalSection(&browsecriticalsection_);
            if (imgitem != nullptr)
            {
                loadcontext_->loader->QueueItem(imgitem, next);
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

void ImgBrowser::QueuePathsAsync(std::vector<std::wstring> paths, INT targetwidth, INT targetheight)
{
    CleanupTargetQueueThreads();

    const auto request = new PathQueueRequest{this, std::move(paths), targetwidth, targetheight};
    const auto thread = CreateThread(nullptr, 0, StaticThreadQueuePaths, request, 0, nullptr);
    if (thread == nullptr)
    {
        delete request;
        return;
    }

    targetqueuethreads_.push_back(thread);
}

void ImgBrowser::QueuePaths(const std::vector<std::wstring>& paths, INT targetwidth, INT targetheight)
{
    for (const auto& filepath : paths)
    {
        if (cancellationflag_)
        {
            break;
        }

        const auto imgformat = ResolveFileFormat(filepath);
        EnterCriticalSection(&browsecriticalsection_);
        const auto active = IsTargetSizeActiveLocked(targetwidth, targetheight);
        const auto imgitem = active ? GetOrCreateCachedItem(filepath, targetwidth, targetheight, imgformat)
                                    : std::shared_ptr<ImgItem>();
        LeaveCriticalSection(&browsecriticalsection_);
        if (imgitem != nullptr)
        {
            loadcontext_->loader->QueueItem(imgitem, FALSE);
        }
    }
}

void ImgBrowser::QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext)
{
    BOOL next = loadnext;
    const auto currentitem = GetOrCreateCachedItem(filepath, targetwidth_, targetheight_, imgformat);
    if (currentitem != nullptr)
    {
        loadcontext_->loader->QueueItem(currentitem, next);
        next = FALSE;
    }

    for (const auto& target_size : preload_target_sizes_)
    {
        if (target_size.width == targetwidth_ && target_size.height == targetheight_)
        {
            continue;
        }

        const auto imgitem = GetOrCreateCachedItem(filepath, target_size.width, target_size.height, imgformat);
        if (imgitem != nullptr)
        {
            loadcontext_->loader->QueueItem(imgitem, next);
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
            loadcontext_->loader->QueueItem(imgitem, TRUE);
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

        loadcontext_->loader->QueueItem(imgitem, TRUE);
    }
}

BOOL ImgBrowser::PreloadTargetSize(INT targetwidth, INT targetheight)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return FALSE;
    }

    EnterCriticalSection(&browsecriticalsection_);
    const auto added = AddPreloadTargetSize(targetwidth, targetheight);
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
    const auto added = AddPreloadTargetSizes(target_sizes, &added_sizes);
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
    stats.sequential = files_.GetSequentialProgress();
    stats.random = files_.GetRandomProgress();
    stats.targetwidth = targetwidth_;
    stats.targetheight = targetheight_;
    stats.sizes = loadcontext_->cache->GetSizeStats();
    LeaveCriticalSection(&browsecriticalsection_);
    stats.loader = loadcontext_->loader->GetStats();

    return stats;
}

ImgFileListProgress ImgBrowser::GetSequentialProgress(const std::wstring& filepath)
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto progress = files_.GetSequentialProgress(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    return progress;
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

void ImgBrowser::BeginRandomCycle()
{
    EnterCriticalSection(&browsecriticalsection_);
    files_.BeginRandomCycle();
    LeaveCriticalSection(&browsecriticalsection_);
}

void ImgBrowser::PreloadFrom(ImgBrowser& source)
{
    if (this == &source)
    {
        return;
    }

    EnterCriticalSection(&source.browsecriticalsection_);
    auto paths = source.files_.PathsFromCurrent();
    LeaveCriticalSection(&source.browsecriticalsection_);

    EnterCriticalSection(&browsecriticalsection_);
    const auto targetwidth = targetwidth_;
    const auto targetheight = targetheight_;
    LeaveCriticalSection(&browsecriticalsection_);

    QueuePathsAsync(std::move(paths), targetwidth, targetheight);
}

BOOL ImgBrowser::MoveToRandom()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToRandom();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToRandomExcluding(const std::vector<std::wstring>& excluded)
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToRandomExcluding(excluded);
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

void ImgBrowser::RemoveCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    if (!filepath.empty())
    {
        loadcontext_->cache->Remove(filepath);
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

    auto imgitem = loadcontext_->cache->Get(filepath, targetwidth, targetheight);
    if (imgitem == nullptr)
    {
        imgitem = loadcontext_->cache->Add(filepath, targetwidth, targetheight, imgformat);
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

DWORD WINAPI ImgBrowser::StaticThreadQueuePaths(void* pathqueuerequest)
{
    const auto request = reinterpret_cast<PathQueueRequest*>(pathqueuerequest);
    if (request != nullptr && request->browser != nullptr)
    {
        request->browser->QueuePaths(request->paths, request->targetwidth, request->targetheight);
    }

    delete request;
    return 0;
}

#include "ImgBrowser.h"
#include "BrowsePath.h"
#include "CriticalSection.h"
#include "FindHandle.h"
#include "ImageFormatResolver.h"
#include "Win32Handle.h"

#include <algorithm>
#include <memory>
#include <utility>

class ImgBrowserCore final : public std::enable_shared_from_this<ImgBrowserCore>
{
  public:
    ImgBrowserCore();
#if defined(IMGVW_TESTING)
    explicit ImgBrowserCore(std::shared_ptr<ImgBrowserTestHooks> test_hooks);
#endif
    ~ImgBrowserCore() = default;
    void ShareLoadContext(const std::shared_ptr<ImgBrowserLoadContext>& context);
    std::shared_ptr<ImgBrowserLoadContext> loadcontext() const;
    BOOL BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight, BOOL clearloadcontext);
    BOOL UpdateTargetSize(INT targetwidth, INT targetheight);
    BOOL BrowseSubFoldersAsync();
    BOOL IsCollectingComplete() const;
    ULONG generation() const;
    ULONG loadgeneration() const;
    bool IsCurrentNotification(WPARAM generation, LPARAM kind) const;
    DWORD GetCollectionError();
    BOOL HasFiles();
    ImgBrowserStopResult StopBrowsing();
    void SetNotificationWindow(HWND hwnd, UINT message);
    std::wstring GetCurrentFilePath();
    std::shared_ptr<ImgItem> GetCurrentItem();
    BOOL MoveToNext();
    BOOL MoveToPrevious();
    BOOL MoveToFirst();
    BOOL MoveToLast();
    BOOL MoveToItem(const std::wstring& filepath);
    BOOL MoveToOrAddItem(const std::wstring& filepath);
    void BeginRandomCycle();
    std::size_t PreloadFrom(ImgBrowserCore& source);
    BOOL MoveToRandom();
    BOOL MoveToRandomExcluding(const std::vector<std::wstring>& excluded);
    void RemoveCurrentItem();
    void ReloadCurrentItem();
    BOOL PreloadTargetSize(INT targetwidth, INT targetheight);
    BOOL PreloadTargetSizes(const std::vector<SIZE>& target_sizes);
    ImgBrowserStats GetStats();
    ImgFileListProgress GetSequentialProgress(const std::wstring& filepath);

  private:
    struct CancellationState
    {
        CancellationState()
            : event(CreateEvent(nullptr, TRUE, FALSE, nullptr)), error(event.valid() ? ERROR_SUCCESS : GetLastError())
        {
        }
        bool Cancelled() const
        {
            return event.valid() && WaitForSingleObject(event.get(), 0) == WAIT_OBJECT_0;
        }
        bool Signal()
        {
            return event.valid() && SetEvent(event.get()) != FALSE;
        }

        Win32Handle event;
        DWORD error{};
    };
    struct TargetSize
    {
        INT width{};
        INT height{};
    };
    struct CollectionRequest
    {
        std::shared_ptr<ImgBrowserCore> browser;
        std::shared_ptr<CancellationState> cancellation;
        std::wstring folderpath;
        volatile LONG recursive{};
        volatile LONG enumeration_error{};
        BOOL subfolders_only{};
        ULONG generation{};
    };
    struct TargetSizeQueueRequest
    {
        std::shared_ptr<ImgBrowserCore> browser;
        std::shared_ptr<CancellationState> cancellation;
        std::vector<TargetSize> sizes;
        BOOL loadnext{};
    };
    struct PathQueueRequest
    {
        std::shared_ptr<ImgBrowserCore> browser;
        std::shared_ptr<CancellationState> cancellation;
        std::vector<std::wstring> paths;
        INT targetwidth{};
        INT targetheight{};
    };

    std::shared_ptr<ImgBrowserLoadContext> loadcontext_{std::make_shared<ImgBrowserLoadContext>()};
    std::wstring folderpath_;
    ImgFileList files_;
    std::vector<std::wstring> folders_;
    Win32Handle collectorthread_;
    std::weak_ptr<CollectionRequest> collectionrequest_;
    std::vector<Win32Handle> targetqueuethreads_;
    std::shared_ptr<CancellationState> targetcancellation_;
    Win32Handle readyevent_;
    CriticalSection browsecriticalsection_;
    INT targetwidth_{};
    INT targetheight_{};
    std::vector<TargetSize> preload_target_sizes_;
    HWND notificationhwnd_{nullptr};
    UINT notificationmessage_{};
    DWORD collection_error_{ERROR_SUCCESS};
    volatile LONG generation_{static_cast<LONG>(NextImgGeneration())};
#if defined(IMGVW_TESTING)
    std::shared_ptr<ImgBrowserTestHooks> test_hooks_;
#endif

    void CollectFile(const std::shared_ptr<CollectionRequest>& request, const std::wstring& filepath,
                     ImgItem::Format imgformat);
    void CollectFolder(const std::shared_ptr<CollectionRequest>& request, const std::wstring& folderpath);
    void CollectSubFolders(const std::shared_ptr<CollectionRequest>& request);
    ImgBrowserOperationStopResult StopCollecting();
    ImgBrowserOperationStopResult StopTargetQueueing();
    void NotifyChanged(ULONG generation = 0);
    static DWORD WINAPI StaticThreadCollect(void* context);
    static DWORD WINAPI StaticThreadQueueTargetSize(void* context);
    static DWORD WINAPI StaticThreadQueuePaths(void* context);
    void Reset();
    BOOL AddPreloadTargetSize(INT targetwidth, INT targetheight);
    BOOL AddPreloadTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes);
    BOOL IsTargetSizeActiveLocked(INT targetwidth, INT targetheight) const;
    void QueueTargetSizeAsync(INT targetwidth, INT targetheight, BOOL loadnext);
    void QueueTargetSizesAsync(const std::vector<TargetSize>& target_sizes, BOOL loadnext);
    void QueueTargetSizes(const std::shared_ptr<CancellationState>& cancellation,
                          const std::vector<TargetSize>& target_sizes, BOOL loadnext);
    void QueuePathsAsync(std::vector<std::wstring> paths, INT targetwidth, INT targetheight);
    void QueuePaths(const std::shared_ptr<CancellationState>& cancellation, const std::vector<std::wstring>& paths,
                    INT targetwidth, INT targetheight);
    void QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext);
    void CleanupTargetQueueThreads();
#if defined(IMGVW_TESTING)
    bool WaitBeforeQueuePath(const std::shared_ptr<CancellationState>& cancellation);
#endif
    ImgItem::Format ResolveFileFormat(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                                   ImgItem::Format imgformat);
};

ImgBrowserCore::ImgBrowserCore() : readyevent_(CreateEvent(nullptr, TRUE, FALSE, nullptr)) {}

#if defined(IMGVW_TESTING)
ImgBrowserCore::ImgBrowserCore(std::shared_ptr<ImgBrowserTestHooks> test_hooks)
    : readyevent_(CreateEvent(nullptr, TRUE, FALSE, nullptr)), test_hooks_(std::move(test_hooks))
{
}
#endif

std::shared_ptr<ImgBrowserLoadContext> ImgBrowserCore::loadcontext() const
{
    return loadcontext_;
}

void ImgBrowserCore::ShareLoadContext(const std::shared_ptr<ImgBrowserLoadContext>& context)
{
    if (context == nullptr || loadcontext_ == context)
    {
        return;
    }

    EnterCriticalSection(browsecriticalsection_.get());
    if (notificationhwnd_ != nullptr)
    {
        loadcontext_->loader->RemoveNotificationWindow(notificationhwnd_);
    }
    loadcontext_ = context;
    if (notificationhwnd_ != nullptr && notificationmessage_ != 0)
    {
        loadcontext_->loader->SetNotificationWindow(notificationhwnd_, notificationmessage_);
    }
    LeaveCriticalSection(browsecriticalsection_.get());
}

void ImgBrowserCore::CollectFile(const std::shared_ptr<CollectionRequest>& request, const std::wstring& filepath,
                                 ImgItem::Format imgformat)
{
    if (request->cancellation->Cancelled())
    {
        return;
    }

    EnterCriticalSection(browsecriticalsection_.get());
    if (request->generation != static_cast<ULONG>(InterlockedCompareExchange(&generation_, 0, 0)))
    {
        LeaveCriticalSection(browsecriticalsection_.get());
        return;
    }

    if (files_.Add(filepath))
    {
        QueueFileForTargetSizes(filepath, imgformat, FALSE);
    }

    if (!SetEvent(readyevent_.get()))
    {
        InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(GetLastError()), ERROR_SUCCESS);
    }
    NotifyChanged(request->generation);

    LeaveCriticalSection(browsecriticalsection_.get());
}

void ImgBrowserCore::CollectFolder(const std::shared_ptr<CollectionRequest>& request, const std::wstring& folderpath)
{
    WIN32_FIND_DATA findfiledata{};
    std::wstring pattern = folderpath + L"*";
    FindHandle findhandle(FindFirstFile(pattern.c_str(), &findfiledata));
    if (!findhandle.valid())
    {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND)
        {
            InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(error), ERROR_SUCCESS);
        }
        return;
    }

    do
    {
        if (request->cancellation->Cancelled())
        {
            break;
        }

        if (findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (lstrcmp(findfiledata.cFileName, L".") != 0 && lstrcmp(findfiledata.cFileName, L"..") != 0)
            {
                std::wstring currentpath(folderpath + findfiledata.cFileName + L"\\");
                if (InterlockedCompareExchange(&request->recursive, 0, 0) != 0)
                {
                    CollectFolder(request, currentpath);
                }
                else
                {
                    EnterCriticalSection(browsecriticalsection_.get());
                    folders_.push_back(currentpath);
                    LeaveCriticalSection(browsecriticalsection_.get());
                }
            }
        }
        else
        {
            std::wstring currentfile(folderpath + findfiledata.cFileName);
            const auto imgformat = ResolveFileFormat(currentfile);
            if (imgformat != ImgItem::Format::Unsupported)
            {
                CollectFile(request, currentfile, imgformat);
            }
        }
    } while (FindNextFile(findhandle.get(), &findfiledata));

    const auto error = GetLastError();
    if (!request->cancellation->Cancelled() && error != ERROR_NO_MORE_FILES)
    {
        InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(error), ERROR_SUCCESS);
    }
}

ImgItem::Format ImgBrowserCore::ResolveFileFormat(const std::wstring& filepath)
{
    return ImageFormatResolver::Resolve(filepath);
}

BOOL ImgBrowserCore::BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight, BOOL clearloadcontext)
{
    if (!readyevent_.valid())
    {
        return FALSE;
    }

    const auto browsepath = ClassifyBrowsePath(path);
    if (!browsepath.Succeeded())
    {
        return FALSE;
    }

    const auto collection_stop = StopCollecting();
    const auto target_stop = StopTargetQueueing();
    if (!collection_stop.Stopped() || !target_stop.Stopped())
    {
        return FALSE;
    }

    if (clearloadcontext)
    {
        loadcontext_->Clear();
    }

    const auto generation = NextImgGeneration();
    InterlockedExchange(&generation_, static_cast<LONG>(generation));
    Reset();

    targetwidth_ = targetwidth;
    targetheight_ = targetheight;
    loadcontext_->loader->PrioritizeTargetSize(targetwidth_, targetheight_);
    const auto cancellation = std::make_shared<CancellationState>();
    if (!cancellation->event.valid())
    {
        return FALSE;
    }
    auto request = std::make_shared<CollectionRequest>();
    request->browser = shared_from_this();
    request->cancellation = cancellation;
    request->generation = generation;
    if (!ResetEvent(readyevent_.get()))
    {
        return FALSE;
    }

    if (browsepath.kind == BrowsePathKind::File)
    {
        folderpath_ = browsepath.folderpath;
        const auto imgformat = ResolveFileFormat(browsepath.filepath);
        if (imgformat != ImgItem::Format::Unsupported)
        {
            CollectFile(request, browsepath.filepath, imgformat);
            MoveToItem(browsepath.filepath);
        }
    }
    else
    {
        folderpath_ = browsepath.folderpath;
    }

    request->folderpath = folderpath_;
    auto context = new std::shared_ptr<CollectionRequest>(request);
    collectorthread_.reset(CreateThread(nullptr, 0, StaticThreadCollect, context, 0, nullptr));
    if (!collectorthread_.valid())
    {
        delete context;
        return FALSE;
    }
    collectionrequest_ = request;
    return TRUE;
}

BOOL ImgBrowserCore::UpdateTargetSize(INT targetwidth, INT targetheight)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return FALSE;
    }

    EnterCriticalSection(browsecriticalsection_.get());
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

    LeaveCriticalSection(browsecriticalsection_.get());
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

BOOL ImgBrowserCore::BrowseSubFoldersAsync()
{
    auto request = collectionrequest_.lock();
    if (request != nullptr && InterlockedCompareExchange(&request->recursive, 1, 0) != 0)
    {
        return FALSE;
    }

    const auto collectorstatus =
        collectorthread_.valid() ? WaitForSingleObject(collectorthread_.get(), 0) : WAIT_OBJECT_0;
    if (collectorstatus == WAIT_TIMEOUT)
    {
        return TRUE;
    }

    if (collectorstatus != WAIT_OBJECT_0)
    {
        // TODO: handle error
        return FALSE;
    }

    EnterCriticalSection(browsecriticalsection_.get());
    const auto hasfolders = !folders_.empty();
    LeaveCriticalSection(browsecriticalsection_.get());
    if (hasfolders)
    {
        auto cancellation = std::make_shared<CancellationState>();
        if (!cancellation->event.valid())
        {
            return FALSE;
        }
        request = std::make_shared<CollectionRequest>();
        request->browser = shared_from_this();
        request->cancellation = cancellation;
        request->recursive = 1;
        request->subfolders_only = TRUE;
        request->generation = static_cast<ULONG>(InterlockedCompareExchange(&generation_, 0, 0));
        if (!ResetEvent(readyevent_.get()))
        {
            return FALSE;
        }
        auto context = new std::shared_ptr<CollectionRequest>(request);
        collectorthread_.reset(CreateThread(nullptr, 0, StaticThreadCollect, context, 0, nullptr));
        if (!collectorthread_.valid())
        {
            delete context;
            return FALSE;
        }
        collectionrequest_ = request;
        return TRUE;
    }

    return FALSE;
}

BOOL ImgBrowserCore::IsCollectingComplete() const
{
    if (!collectorthread_.valid())
    {
        return TRUE;
    }

    return WaitForSingleObject(collectorthread_.get(), 0) == WAIT_OBJECT_0 ? TRUE : FALSE;
}

ULONG ImgBrowserCore::generation() const
{
    return static_cast<ULONG>(InterlockedCompareExchange(const_cast<volatile LONG*>(&generation_), 0, 0));
}

ULONG ImgBrowserCore::loadgeneration() const
{
    return loadcontext_->CurrentGeneration();
}

bool ImgBrowserCore::IsCurrentNotification(WPARAM generation, LPARAM kind) const
{
    switch (static_cast<ImgNotificationKind>(kind))
    {
    case ImgNotificationKind::BrowserState:
        return generation == this->generation();
    case ImgNotificationKind::LoadComplete:
        return generation == loadgeneration();
    default:
        return false;
    }
}

DWORD ImgBrowserCore::GetCollectionError()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto error = collection_error_;
    LeaveCriticalSection(browsecriticalsection_.get());
    return error;
}

BOOL ImgBrowserCore::HasFiles()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto hasfiles = !files_.Empty();
    LeaveCriticalSection(browsecriticalsection_.get());
    return hasfiles ? TRUE : FALSE;
}

ImgBrowserOperationStopResult ImgBrowserCore::StopCollecting()
{
    if (!collectorthread_.valid())
    {
        return {ImgBrowserStopStatus::AlreadyStopped, ERROR_SUCCESS};
    }

    const auto request = collectionrequest_.lock();
    if (request != nullptr && !request->cancellation->Signal())
    {
        return {ImgBrowserStopStatus::SignalFailed, GetLastError()};
    }

    const DWORD timeoutMs = 3000;
    const DWORD waitResult = WaitForSingleObject(collectorthread_.get(), timeoutMs);
    if (waitResult == WAIT_TIMEOUT)
    {
#if defined(IMGVW_DEBUG)
        OutputDebugString(L"ImgBrowser::StopCollecting: Warning: collector thread did not terminate within timeout.\n");
#endif
        return {ImgBrowserStopStatus::TimedOut, ERROR_SUCCESS};
    }
    else if (waitResult == WAIT_FAILED)
    {
        const DWORD error = GetLastError();
#if defined(IMGVW_DEBUG)
        WCHAR buf[256];
        swprintf_s(buf, L"ImgBrowser::StopCollecting: WaitForSingleObject failed with error 0x%08lX\n",
                   static_cast<unsigned long>(error));
        OutputDebugString(buf);
#endif
        return {ImgBrowserStopStatus::WaitFailed, error};
    }

    collectorthread_.reset();
    collectionrequest_.reset();
    return {ImgBrowserStopStatus::Stopped, ERROR_SUCCESS};
}

ImgBrowserStopResult ImgBrowserCore::StopBrowsing()
{
    ImgBrowserStopResult result;
    result.collection = StopCollecting();
    result.target_queue = StopTargetQueueing();
    EnterCriticalSection(browsecriticalsection_.get());
    const auto notificationhwnd = notificationhwnd_;
    notificationhwnd_ = nullptr;
    notificationmessage_ = 0;
    LeaveCriticalSection(browsecriticalsection_.get());
    if (notificationhwnd != nullptr)
    {
        loadcontext_->loader->RemoveNotificationWindow(notificationhwnd);
    }
    return result;
}

ImgBrowserOperationStopResult ImgBrowserCore::StopTargetQueueing()
{
    if (targetqueuethreads_.empty())
    {
        targetcancellation_.reset();
        return {ImgBrowserStopStatus::AlreadyStopped, ERROR_SUCCESS};
    }

    if (targetcancellation_ != nullptr && !targetcancellation_->Signal())
    {
        return {ImgBrowserStopStatus::SignalFailed, GetLastError()};
    }

    const DWORD timeoutMs = 3000;
    const DWORD start = GetTickCount();
    for (const auto& thread : targetqueuethreads_)
    {
        if (!thread.valid())
        {
            continue;
        }

        const DWORD elapsed = GetTickCount() - start;
        const DWORD remaining = elapsed < timeoutMs ? timeoutMs - elapsed : 0;
        const DWORD waitResult = WaitForSingleObject(thread.get(), remaining);
        if (waitResult == WAIT_TIMEOUT)
        {
#if defined(IMGVW_DEBUG)
            OutputDebugString(L"ImgBrowser::StopTargetQueueing: Warning: target queue thread did not terminate.\n");
#endif
            return {ImgBrowserStopStatus::TimedOut, ERROR_SUCCESS};
        }
        else if (waitResult == WAIT_FAILED)
        {
            const DWORD error = GetLastError();
#if defined(IMGVW_DEBUG)
            WCHAR buf[256];
            swprintf_s(buf, L"ImgBrowser::StopTargetQueueing: WaitForSingleObject failed with error 0x%08lX\n",
                       static_cast<unsigned long>(error));
            OutputDebugString(buf);
#endif
            return {ImgBrowserStopStatus::WaitFailed, error};
        }
    }

    targetqueuethreads_.clear();
    targetcancellation_.reset();
    return {ImgBrowserStopStatus::Stopped, ERROR_SUCCESS};
}

void ImgBrowserCore::SetNotificationWindow(HWND hwnd, UINT message)
{
    EnterCriticalSection(browsecriticalsection_.get());
    if (notificationhwnd_ != nullptr && notificationhwnd_ != hwnd)
    {
        loadcontext_->loader->RemoveNotificationWindow(notificationhwnd_);
    }
    notificationhwnd_ = hwnd;
    notificationmessage_ = message;
    loadcontext_->loader->SetNotificationWindow(hwnd, message);
    LeaveCriticalSection(browsecriticalsection_.get());
}

void ImgBrowserCore::Reset()
{
    files_.Clear();
    folderpath_.clear();
    folders_.clear();
    preload_target_sizes_.clear();
    collection_error_ = ERROR_SUCCESS;
}

BOOL ImgBrowserCore::AddPreloadTargetSize(INT targetwidth, INT targetheight)
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

BOOL ImgBrowserCore::AddPreloadTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes)
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

BOOL ImgBrowserCore::IsTargetSizeActiveLocked(INT targetwidth, INT targetheight) const
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

void ImgBrowserCore::QueueTargetSizes(const std::shared_ptr<CancellationState>& cancellation,
                                      const std::vector<TargetSize>& target_sizes, BOOL loadnext)
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto paths = files_.PathsFromCurrent();
    LeaveCriticalSection(browsecriticalsection_.get());

    BOOL next = loadnext;
    for (const auto& filepath : paths)
    {
        if (cancellation->Cancelled())
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
            if (cancellation->Cancelled())
            {
                break;
            }

            EnterCriticalSection(browsecriticalsection_.get());
            const auto active = IsTargetSizeActiveLocked(target_size.width, target_size.height);
            const auto imgitem = active
                                     ? GetOrCreateCachedItem(filepath, target_size.width, target_size.height, imgformat)
                                     : std::shared_ptr<ImgItem>();
            LeaveCriticalSection(browsecriticalsection_.get());
            if (imgitem != nullptr)
            {
                loadcontext_->loader->QueueItem(imgitem, next, loadcontext_->CurrentGeneration());
                next = FALSE;
                NotifyChanged();
            }
        }
    }
}

#if defined(IMGVW_TESTING)
bool ImgBrowserCore::WaitBeforeQueuePath(const std::shared_ptr<CancellationState>& cancellation)
{
    if (test_hooks_ != nullptr && test_hooks_->path_queue_entered != nullptr &&
        test_hooks_->path_queue_continue != nullptr)
    {
        SetEvent(test_hooks_->path_queue_entered);
        const HANDLE events[] = {cancellation->event.get(), test_hooks_->path_queue_continue};
        const auto wait_result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (test_hooks_->path_queue_resumed != nullptr)
        {
            SetEvent(test_hooks_->path_queue_resumed);
        }
        return wait_result == WAIT_OBJECT_0 + 1;
    }
    return true;
}
#endif

void ImgBrowserCore::QueueTargetSizeAsync(INT targetwidth, INT targetheight, BOOL loadnext)
{
    QueueTargetSizesAsync(std::vector<TargetSize>{{targetwidth, targetheight}}, loadnext);
}

void ImgBrowserCore::QueueTargetSizesAsync(const std::vector<TargetSize>& target_sizes, BOOL loadnext)
{
    CleanupTargetQueueThreads();
    if (targetcancellation_ == nullptr)
    {
        targetcancellation_ = std::make_shared<CancellationState>();
    }
    if (!targetcancellation_->event.valid())
    {
        targetcancellation_.reset();
        return;
    }

    const auto request = new TargetSizeQueueRequest{shared_from_this(), targetcancellation_, target_sizes, loadnext};
    const auto thread = CreateThread(nullptr, 0, StaticThreadQueueTargetSize, request, 0, nullptr);
    if (thread == nullptr)
    {
        delete request;
        return;
    }

    targetqueuethreads_.emplace_back(thread);
}

void ImgBrowserCore::QueuePathsAsync(std::vector<std::wstring> paths, INT targetwidth, INT targetheight)
{
    CleanupTargetQueueThreads();
    if (targetcancellation_ == nullptr)
    {
        targetcancellation_ = std::make_shared<CancellationState>();
    }
    if (!targetcancellation_->event.valid())
    {
        targetcancellation_.reset();
        return;
    }

    const auto request =
        new PathQueueRequest{shared_from_this(), targetcancellation_, std::move(paths), targetwidth, targetheight};
    const auto thread = CreateThread(nullptr, 0, StaticThreadQueuePaths, request, 0, nullptr);
    if (thread == nullptr)
    {
        delete request;
        return;
    }

    targetqueuethreads_.emplace_back(thread);
}

void ImgBrowserCore::QueuePaths(const std::shared_ptr<CancellationState>& cancellation,
                                const std::vector<std::wstring>& paths, INT targetwidth, INT targetheight)
{
    for (const auto& filepath : paths)
    {
        if (cancellation->Cancelled())
        {
            break;
        }
#if defined(IMGVW_TESTING)
        if (!WaitBeforeQueuePath(cancellation))
        {
            break;
        }
#endif

        const auto imgformat = ResolveFileFormat(filepath);
        EnterCriticalSection(browsecriticalsection_.get());
        const auto active = IsTargetSizeActiveLocked(targetwidth, targetheight);
        const auto imgitem =
            active ? GetOrCreateCachedItem(filepath, targetwidth, targetheight, imgformat) : std::shared_ptr<ImgItem>();
        LeaveCriticalSection(browsecriticalsection_.get());
        if (imgitem != nullptr)
        {
            loadcontext_->loader->QueueItem(imgitem, FALSE, loadcontext_->CurrentGeneration());
            NotifyChanged();
        }
    }
}

void ImgBrowserCore::QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext)
{
    BOOL next = loadnext;
    const auto currentitem = GetOrCreateCachedItem(filepath, targetwidth_, targetheight_, imgformat);
    if (currentitem != nullptr)
    {
        loadcontext_->loader->QueueItem(currentitem, next, loadcontext_->CurrentGeneration());
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
            loadcontext_->loader->QueueItem(imgitem, next, loadcontext_->CurrentGeneration());
            next = FALSE;
        }
    }
}

void ImgBrowserCore::CleanupTargetQueueThreads()
{
    auto thread = targetqueuethreads_.begin();
    while (thread != targetqueuethreads_.end())
    {
        if (!thread->valid() || WaitForSingleObject(thread->get(), 0) == WAIT_OBJECT_0)
        {
            thread = targetqueuethreads_.erase(thread);
        }
        else
        {
            ++thread;
        }
    }
    if (targetqueuethreads_.empty())
    {
        targetcancellation_.reset();
    }
}

std::wstring ImgBrowserCore::GetCurrentFilePath()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto filepath = files_.CurrentPath();
    LeaveCriticalSection(browsecriticalsection_.get());
    return filepath;
}

std::shared_ptr<ImgItem> ImgBrowserCore::GetCurrentItem()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto filepath = files_.CurrentPath();
    const auto imgitem = filepath.empty() ? std::shared_ptr<ImgItem>() : GetOrCreateCachedItem(filepath);
    LeaveCriticalSection(browsecriticalsection_.get());
    if (imgitem != nullptr)
    {
        if (imgitem->status() == ImgItem::Status::Queued)
        {
            loadcontext_->loader->QueueItem(imgitem, TRUE, loadcontext_->CurrentGeneration());
        }

        return imgitem;
    }

    return {};
}

void ImgBrowserCore::ReloadCurrentItem()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto filepath = files_.CurrentPath();
    const auto imgitem = filepath.empty() ? std::shared_ptr<ImgItem>() : GetOrCreateCachedItem(filepath);
    LeaveCriticalSection(browsecriticalsection_.get());
    if (imgitem != nullptr)
    {
        if (imgitem->status() != ImgItem::Status::Queued)
        {
            imgitem->Unload();
        }

        loadcontext_->loader->QueueItem(imgitem, TRUE, loadcontext_->CurrentGeneration());
    }
}

BOOL ImgBrowserCore::PreloadTargetSize(INT targetwidth, INT targetheight)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return FALSE;
    }

    EnterCriticalSection(browsecriticalsection_.get());
    const auto added = AddPreloadTargetSize(targetwidth, targetheight);
    LeaveCriticalSection(browsecriticalsection_.get());
    if (added)
    {
        QueueTargetSizeAsync(targetwidth, targetheight, FALSE);
    }

    return added;
}

BOOL ImgBrowserCore::PreloadTargetSizes(const std::vector<SIZE>& target_sizes)
{
    std::vector<TargetSize> added_sizes;
    EnterCriticalSection(browsecriticalsection_.get());
    const auto added = AddPreloadTargetSizes(target_sizes, &added_sizes);
    LeaveCriticalSection(browsecriticalsection_.get());
    if (added)
    {
        QueueTargetSizesAsync(added_sizes, FALSE);
    }

    return added;
}

ImgBrowserStats ImgBrowserCore::GetStats()
{
    ImgBrowserStats stats;
    EnterCriticalSection(browsecriticalsection_.get());
    stats.found_images = files_.Size();
    stats.sequential = files_.GetSequentialProgress();
    stats.random = files_.GetRandomProgress();
    stats.targetwidth = targetwidth_;
    stats.targetheight = targetheight_;
    stats.sizes = loadcontext_->cache->GetSizeStats();
    LeaveCriticalSection(browsecriticalsection_.get());
    stats.loader = loadcontext_->loader->GetStats();

    return stats;
}

ImgFileListProgress ImgBrowserCore::GetSequentialProgress(const std::wstring& filepath)
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto progress = files_.GetSequentialProgress(filepath);
    LeaveCriticalSection(browsecriticalsection_.get());
    return progress;
}

BOOL ImgBrowserCore::MoveToNext()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveToNext();
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

BOOL ImgBrowserCore::MoveToPrevious()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveToPrevious();
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

BOOL ImgBrowserCore::MoveToFirst()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveToFirst();
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

BOOL ImgBrowserCore::MoveToLast()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveToLast();
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

BOOL ImgBrowserCore::MoveToItem(const std::wstring& filepath)
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveTo(filepath);
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

BOOL ImgBrowserCore::MoveToOrAddItem(const std::wstring& filepath)
{
    const auto imgformat = ResolveFileFormat(filepath);
    if (imgformat == ImgItem::Format::Unsupported)
    {
        return FALSE;
    }

    EnterCriticalSection(browsecriticalsection_.get());
    if (files_.Add(filepath))
    {
        QueueFileForTargetSizes(filepath, imgformat, FALSE);
    }

    const auto moveSuccess = files_.MoveTo(filepath);
    LeaveCriticalSection(browsecriticalsection_.get());
    if (moveSuccess)
    {
        NotifyChanged();
    }

    return moveSuccess;
}

void ImgBrowserCore::BeginRandomCycle()
{
    EnterCriticalSection(browsecriticalsection_.get());
    files_.BeginRandomCycle();
    LeaveCriticalSection(browsecriticalsection_.get());
}

std::size_t ImgBrowserCore::PreloadFrom(ImgBrowserCore& source)
{
    if (this == &source)
    {
        return 0;
    }

    EnterCriticalSection(source.browsecriticalsection_.get());
    auto paths = source.files_.PathsFromCurrent();
    LeaveCriticalSection(source.browsecriticalsection_.get());

    EnterCriticalSection(browsecriticalsection_.get());
    const auto targetwidth = targetwidth_;
    const auto targetheight = targetheight_;
    LeaveCriticalSection(browsecriticalsection_.get());

    const auto path_count = paths.size();
    QueuePathsAsync(std::move(paths), targetwidth, targetheight);
    return path_count;
}

BOOL ImgBrowserCore::MoveToRandom()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveToRandom();
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

BOOL ImgBrowserCore::MoveToRandomExcluding(const std::vector<std::wstring>& excluded)
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto moveSuccess = files_.MoveToRandomExcluding(excluded);
    LeaveCriticalSection(browsecriticalsection_.get());
    return moveSuccess;
}

void ImgBrowserCore::RemoveCurrentItem()
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto filepath = files_.CurrentPath();
    if (!filepath.empty())
    {
        loadcontext_->cache->Remove(filepath);
        files_.RemoveCurrent();
    }

    LeaveCriticalSection(browsecriticalsection_.get());
}

void ImgBrowserCore::CollectSubFolders(const std::shared_ptr<CollectionRequest>& request)
{
    EnterCriticalSection(browsecriticalsection_.get());
    const auto folders = folders_;
    LeaveCriticalSection(browsecriticalsection_.get());
    for (const auto& folder : folders)
    {
        if (request->cancellation->Cancelled())
        {
            break;
        }
        CollectFolder(request, folder);
    }

    EnterCriticalSection(browsecriticalsection_.get());
    folders_.clear();
    LeaveCriticalSection(browsecriticalsection_.get());
}

std::shared_ptr<ImgItem> ImgBrowserCore::GetOrCreateCachedItem(const std::wstring& filepath)
{
    const auto imgformat = ResolveFileFormat(filepath);
    return GetOrCreateCachedItem(filepath, targetwidth_, targetheight_, imgformat);
}

std::shared_ptr<ImgItem> ImgBrowserCore::GetOrCreateCachedItem(const std::wstring& filepath, INT targetwidth,
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

void ImgBrowserCore::NotifyChanged(ULONG generation)
{
    EnterCriticalSection(browsecriticalsection_.get());
    if (generation == 0)
    {
        generation = static_cast<ULONG>(InterlockedCompareExchange(&generation_, 0, 0));
    }
    if (notificationhwnd_ != nullptr && notificationmessage_ != 0)
    {
        PostMessage(notificationhwnd_, notificationmessage_, generation,
                    static_cast<LPARAM>(ImgNotificationKind::BrowserState));
    }
    LeaveCriticalSection(browsecriticalsection_.get());
}

DWORD WINAPI ImgBrowserCore::StaticThreadCollect(void* context)
{
    std::unique_ptr<std::shared_ptr<CollectionRequest>> holder(
        reinterpret_cast<std::shared_ptr<CollectionRequest>*>(context));
    const auto request = holder != nullptr ? *holder : std::shared_ptr<CollectionRequest>();
    if (request == nullptr || request->browser == nullptr)
    {
        return 0;
    }

    if (request->subfolders_only)
    {
        request->browser->CollectSubFolders(request);
    }
    else
    {
        request->browser->CollectFolder(request, request->folderpath);
        if (InterlockedCompareExchange(&request->recursive, 0, 0) != 0)
        {
            request->browser->CollectSubFolders(request);
        }
    }

    if (!SetEvent(request->browser->readyevent_.get()))
    {
        InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(GetLastError()), ERROR_SUCCESS);
    }
    EnterCriticalSection(request->browser->browsecriticalsection_.get());
    if (request->generation ==
        static_cast<ULONG>(InterlockedCompareExchange(&request->browser->generation_, 0, 0)))
    {
        request->browser->collection_error_ = static_cast<DWORD>(request->enumeration_error);
    }
    LeaveCriticalSection(request->browser->browsecriticalsection_.get());
    request->browser->NotifyChanged(request->generation);

    return 0;
}

DWORD WINAPI ImgBrowserCore::StaticThreadQueueTargetSize(void* targetsizequeuerequest)
{
    const auto request = reinterpret_cast<TargetSizeQueueRequest*>(targetsizequeuerequest);
    if (request != nullptr && request->browser != nullptr)
    {
        request->browser->QueueTargetSizes(request->cancellation, request->sizes, request->loadnext);
    }

    delete request;
    return 0;
}

DWORD WINAPI ImgBrowserCore::StaticThreadQueuePaths(void* pathqueuerequest)
{
    const auto request = reinterpret_cast<PathQueueRequest*>(pathqueuerequest);
    if (request != nullptr && request->browser != nullptr)
    {
        request->browser->QueuePaths(request->cancellation, request->paths, request->targetwidth,
                                     request->targetheight);
    }

    delete request;
    return 0;
}

ImgBrowser::ImgBrowser() : core_(std::make_shared<ImgBrowserCore>()) {}

#if defined(IMGVW_TESTING)
ImgBrowser::ImgBrowser(const std::shared_ptr<ImgBrowserTestHooks>& test_hooks)
    : core_(std::make_shared<ImgBrowserCore>(test_hooks))
{
}
#endif

ImgBrowser::~ImgBrowser()
{
    core_->StopBrowsing();
}

void ImgBrowser::ShareLoadContext(const std::shared_ptr<ImgBrowserLoadContext>& context)
{
    core_->ShareLoadContext(context);
}

std::shared_ptr<ImgBrowserLoadContext> ImgBrowser::loadcontext() const
{
    return core_->loadcontext();
}

BOOL ImgBrowser::BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight, BOOL clearloadcontext)
{
    return core_->BrowseAsync(path, targetwidth, targetheight, clearloadcontext);
}

BOOL ImgBrowser::UpdateTargetSize(INT targetwidth, INT targetheight)
{
    return core_->UpdateTargetSize(targetwidth, targetheight);
}

BOOL ImgBrowser::BrowseSubFoldersAsync()
{
    return core_->BrowseSubFoldersAsync();
}

BOOL ImgBrowser::IsCollectingComplete() const
{
    return core_->IsCollectingComplete();
}

ULONG ImgBrowser::generation() const
{
    return core_->generation();
}

ULONG ImgBrowser::loadgeneration() const
{
    return core_->loadgeneration();
}

bool ImgBrowser::IsCurrentNotification(WPARAM generation, LPARAM kind) const
{
    return core_->IsCurrentNotification(generation, kind);
}

DWORD ImgBrowser::GetCollectionError()
{
    return core_->GetCollectionError();
}

BOOL ImgBrowser::HasFiles()
{
    return core_->HasFiles();
}

ImgBrowserStopResult ImgBrowser::StopBrowsing()
{
    return core_->StopBrowsing();
}

void ImgBrowser::SetNotificationWindow(HWND hwnd, UINT message)
{
    core_->SetNotificationWindow(hwnd, message);
}

std::wstring ImgBrowser::GetCurrentFilePath()
{
    return core_->GetCurrentFilePath();
}

std::shared_ptr<ImgItem> ImgBrowser::GetCurrentItem()
{
    return core_->GetCurrentItem();
}

BOOL ImgBrowser::MoveToNext()
{
    return core_->MoveToNext();
}

BOOL ImgBrowser::MoveToPrevious()
{
    return core_->MoveToPrevious();
}

BOOL ImgBrowser::MoveToFirst()
{
    return core_->MoveToFirst();
}

BOOL ImgBrowser::MoveToLast()
{
    return core_->MoveToLast();
}

BOOL ImgBrowser::MoveToItem(const std::wstring& filepath)
{
    return core_->MoveToItem(filepath);
}

BOOL ImgBrowser::MoveToOrAddItem(const std::wstring& filepath)
{
    return core_->MoveToOrAddItem(filepath);
}

void ImgBrowser::BeginRandomCycle()
{
    core_->BeginRandomCycle();
}

std::size_t ImgBrowser::PreloadFrom(ImgBrowser& source)
{
    return core_->PreloadFrom(*source.core_);
}

BOOL ImgBrowser::MoveToRandom()
{
    return core_->MoveToRandom();
}

BOOL ImgBrowser::MoveToRandomExcluding(const std::vector<std::wstring>& excluded)
{
    return core_->MoveToRandomExcluding(excluded);
}

void ImgBrowser::RemoveCurrentItem()
{
    core_->RemoveCurrentItem();
}

void ImgBrowser::ReloadCurrentItem()
{
    core_->ReloadCurrentItem();
}

BOOL ImgBrowser::PreloadTargetSize(INT targetwidth, INT targetheight)
{
    return core_->PreloadTargetSize(targetwidth, targetheight);
}

BOOL ImgBrowser::PreloadTargetSizes(const std::vector<SIZE>& target_sizes)
{
    return core_->PreloadTargetSizes(target_sizes);
}

ImgBrowserStats ImgBrowser::GetStats()
{
    return core_->GetStats();
}

ImgFileListProgress ImgBrowser::GetSequentialProgress(const std::wstring& filepath)
{
    return core_->GetSequentialProgress(filepath);
}

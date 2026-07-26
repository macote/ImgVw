#include "ImgBrowser.h"
#include "BrowsePath.h"
#include "BrowseSession.h"
#include "CriticalSection.h"
#include "FolderScanner.h"
#include "ImgFormatResolver.h"
#include "PreloadScheduler.h"
#include "Win32Handle.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace
{
DWORD StopError(const ImgBrowserOperationStopResult& result)
{
    return result.win32_error != ERROR_SUCCESS
               ? result.win32_error
               : (result.status == ImgBrowserStopStatus::TimedOut ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED);
}
} // namespace

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
    ImgBrowserStartResult StartBrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight,
                                           BOOL clearloadcontext);
    BOOL UpdateTargetSize(INT targetwidth, INT targetheight);
    ImgBrowserStartResult StartBrowseSubFoldersAsync();
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
    using CancellationState = BrowseSessionCancellation;
    using TargetSize = PreloadSize;
    struct CollectionRequest
    {
        std::shared_ptr<ImgBrowserCore> browser;
        std::shared_ptr<BrowseSessionCollection> collection;
        volatile LONG enumeration_error{};
    };
    std::shared_ptr<ImgBrowserLoadContext> loadcontext_{std::make_shared<ImgBrowserLoadContext>()};
    BrowseSession session_;
    std::wstring& folderpath_{session_.folderpath()};
    ImgFileList& files_{session_.files()};
    std::vector<std::wstring>& folders_{session_.folders()};
    PreloadScheduler preload_scheduler_;
    std::weak_ptr<CollectionRequest> collectionrequest_;
    CriticalSection browsecriticalsection_;
    INT targetwidth_{};
    INT targetheight_{};
    HWND notificationhwnd_{nullptr};
    UINT notificationmessage_{};
#if defined(IMGVW_TESTING)
    std::shared_ptr<ImgBrowserTestHooks> test_hooks_;
#endif

    void CollectFile(const std::shared_ptr<CollectionRequest>& request, const std::wstring& filepath,
                     ImgItem::Format imgformat);
    void CollectSubFolders(const std::shared_ptr<CollectionRequest>& request);
    ImgBrowserOperationStopResult StopCollecting();
    ImgBrowserOperationStopResult StopTargetQueueing();
    void NotifyChanged(ULONG generation = 0);
    static DWORD WINAPI StaticThreadCollect(void* context);
    void Reset(ULONG generation);
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
#if defined(IMGVW_TESTING)
    bool WaitBeforeQueuePath(const std::shared_ptr<CancellationState>& cancellation);
#endif
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath);
    std::shared_ptr<ImgItem> GetOrCreateCachedItem(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                                   ImgItem::Format imgformat);
};

ImgBrowserCore::ImgBrowserCore() = default;

#if defined(IMGVW_TESTING)
ImgBrowserCore::ImgBrowserCore(std::shared_ptr<ImgBrowserTestHooks> test_hooks) : test_hooks_(std::move(test_hooks)) {}
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
    if (request->collection->cancellation->Cancelled())
    {
        return;
    }

    EnterCriticalSection(browsecriticalsection_.get());
    if (request->collection->generation != session_.generation())
    {
        LeaveCriticalSection(browsecriticalsection_.get());
        return;
    }

    if (files_.Add(filepath))
    {
        QueueFileForTargetSizes(filepath, imgformat, FALSE);
    }

    if (!session_.SignalReadyEvent())
    {
        InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(GetLastError()), ERROR_SUCCESS);
    }
    NotifyChanged(request->collection->generation);

    LeaveCriticalSection(browsecriticalsection_.get());
}

ImgBrowserStartResult ImgBrowserCore::StartBrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight,
                                                       BOOL clearloadcontext)
{
    if (targetwidth <= 0 || targetheight <= 0)
    {
        return {ImgBrowserStartStatus::InvalidTargetSize, ERROR_INVALID_PARAMETER};
    }
    if (!session_.ReadyEventValid())
    {
        return {ImgBrowserStartStatus::ReadyEventUnavailable, session_.ready_event_error()};
    }

    const auto browsepath = ClassifyBrowsePath(path);
    if (!browsepath.Succeeded())
    {
        return {ImgBrowserStartStatus::InvalidPath, browsepath.win32_error};
    }

    const auto collection_stop = StopCollecting();
    const auto target_stop = StopTargetQueueing();
    if (!collection_stop.Stopped())
    {
        return {ImgBrowserStartStatus::StopFailed, StopError(collection_stop)};
    }
    if (!target_stop.Stopped())
    {
        return {ImgBrowserStartStatus::StopFailed, StopError(target_stop)};
    }

    if (clearloadcontext)
    {
        loadcontext_->Clear();
    }

    const auto generation = NextImgGeneration();
    Reset(generation);

    targetwidth_ = targetwidth;
    targetheight_ = targetheight;
    preload_scheduler_.SetActiveTargetSize(targetwidth_, targetheight_);
    loadcontext_->loader->PrioritizeTargetSize(targetwidth_, targetheight_);
    const auto collection = session_.BeginCollection(generation);
    if (!collection->cancellation->valid())
    {
        return {ImgBrowserStartStatus::CreateCancellationEventFailed, collection->cancellation->error()};
    }
    auto request = std::make_shared<CollectionRequest>();
    request->browser = shared_from_this();
    request->collection = collection;
    BOOL readyeventreset{};
#if defined(IMGVW_TESTING)
    if (test_hooks_ != nullptr && test_hooks_->fail_ready_reset)
    {
        SetLastError(ERROR_WRITE_FAULT);
        readyeventreset = FALSE;
    }
    else
#endif
    {
        readyeventreset = session_.ResetReadyEvent() ? TRUE : FALSE;
    }
    if (!readyeventreset)
    {
        return {ImgBrowserStartStatus::ResetReadyEventFailed, GetLastError()};
    }

    if (browsepath.kind == BrowsePathKind::File)
    {
        folderpath_ = browsepath.folderpath;
        const auto imgformat = ImgFormatResolver::Resolve(browsepath.filepath);
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

    collection->folderpath = folderpath_;
    auto context = new std::shared_ptr<CollectionRequest>(request);
#if defined(IMGVW_TESTING)
    if (test_hooks_ != nullptr && test_hooks_->fail_collector_thread_create)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        session_.ClearCollectorThread();
    }
    else
#endif
    {
        const auto thread = CreateThread(nullptr, 0, StaticThreadCollect, context, 0, nullptr);
        session_.SetCollectorThread(thread, collection->cancellation);
    }
    if (!session_.HasCollectorThread())
    {
        const auto error = GetLastError();
        delete context;
        session_.ClearCollectorThread();
        return {ImgBrowserStartStatus::CreateThreadFailed, error};
    }
    collectionrequest_ = request;
    return {ImgBrowserStartStatus::Started, ERROR_SUCCESS};
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
        keep_previous_target = IsTargetSizeActiveLocked(previouswidth, previousheight);
        targetwidth_ = targetwidth;
        targetheight_ = targetheight;
        preload_scheduler_.SetActiveTargetSize(targetwidth_, targetheight_);
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

ImgBrowserStartResult ImgBrowserCore::StartBrowseSubFoldersAsync()
{
    auto request = collectionrequest_.lock();
    if (request != nullptr && InterlockedCompareExchange(&request->collection->recursive, 1, 0) != 0)
    {
        return {ImgBrowserStartStatus::AlreadyRecursive, ERROR_SUCCESS};
    }

    DWORD collectorstatus{};
#if defined(IMGVW_TESTING)
    if (test_hooks_ != nullptr && test_hooks_->fail_collector_wait)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        collectorstatus = WAIT_FAILED;
    }
    else
#endif
    {
        collectorstatus =
            session_.HasCollectorThread() ? WaitForSingleObject(session_.collector_thread(), 0) : WAIT_OBJECT_0;
    }
    if (collectorstatus == WAIT_TIMEOUT)
    {
        return {ImgBrowserStartStatus::JoinedActiveCollection, ERROR_SUCCESS};
    }

    if (collectorstatus != WAIT_OBJECT_0)
    {
        return {ImgBrowserStartStatus::WaitFailed,
                collectorstatus == WAIT_FAILED ? GetLastError() : ERROR_INVALID_FUNCTION};
    }

    EnterCriticalSection(browsecriticalsection_.get());
    const auto hasfolders = !folders_.empty();
    LeaveCriticalSection(browsecriticalsection_.get());
    if (hasfolders)
    {
        const auto collection = session_.BeginCollection(session_.generation());
        if (!collection->cancellation->valid())
        {
            return {ImgBrowserStartStatus::CreateCancellationEventFailed, collection->cancellation->error()};
        }
        request = std::make_shared<CollectionRequest>();
        request->browser = shared_from_this();
        request->collection = collection;
        collection->recursive = 1;
        collection->subfolders_only = TRUE;
        BOOL readyeventreset{};
#if defined(IMGVW_TESTING)
        if (test_hooks_ != nullptr && test_hooks_->fail_ready_reset)
        {
            SetLastError(ERROR_WRITE_FAULT);
            readyeventreset = FALSE;
        }
        else
#endif
        {
            readyeventreset = session_.ResetReadyEvent() ? TRUE : FALSE;
        }
        if (!readyeventreset)
        {
            return {ImgBrowserStartStatus::ResetReadyEventFailed, GetLastError()};
        }
        auto context = new std::shared_ptr<CollectionRequest>(request);
#if defined(IMGVW_TESTING)
        if (test_hooks_ != nullptr && test_hooks_->fail_collector_thread_create)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            session_.ClearCollectorThread();
        }
        else
#endif
        {
            const auto thread = CreateThread(nullptr, 0, StaticThreadCollect, context, 0, nullptr);
            session_.SetCollectorThread(thread, collection->cancellation);
        }
        if (!session_.HasCollectorThread())
        {
            const auto error = GetLastError();
            delete context;
            session_.ClearCollectorThread();
            return {ImgBrowserStartStatus::CreateThreadFailed, error};
        }
        collectionrequest_ = request;
        return {ImgBrowserStartStatus::Started, ERROR_SUCCESS};
    }

    return {ImgBrowserStartStatus::NoSubfolders, ERROR_SUCCESS};
}

BOOL ImgBrowserCore::IsCollectingComplete() const
{
    if (!session_.HasCollectorThread())
    {
        return TRUE;
    }

    return WaitForSingleObject(session_.collector_thread(), 0) == WAIT_OBJECT_0 ? TRUE : FALSE;
}

ULONG ImgBrowserCore::generation() const
{
    return session_.generation();
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
    const auto error = session_.collection_error();
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
    if (!session_.HasCollectorThread())
    {
        return {ImgBrowserStopStatus::AlreadyStopped, ERROR_SUCCESS};
    }

    const auto cancellation = session_.cancellation();
    if (cancellation != nullptr && !cancellation->Signal())
    {
        return {ImgBrowserStopStatus::SignalFailed, GetLastError()};
    }

    const DWORD timeoutMs = 3000;
    const DWORD waitResult = WaitForSingleObject(session_.collector_thread(), timeoutMs);
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

    session_.ClearCollectorThread();
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
    const auto result = preload_scheduler_.Stop(3000);
    switch (result.status)
    {
    case PreloadSchedulerStopStatus::AlreadyStopped:
        return {ImgBrowserStopStatus::AlreadyStopped, result.win32_error};
    case PreloadSchedulerStopStatus::Stopped:
        return {ImgBrowserStopStatus::Stopped, result.win32_error};
    case PreloadSchedulerStopStatus::SignalFailed:
        return {ImgBrowserStopStatus::SignalFailed, result.win32_error};
    case PreloadSchedulerStopStatus::TimedOut:
#if defined(IMGVW_DEBUG)
        OutputDebugString(L"ImgBrowser::StopTargetQueueing: Warning: target queue thread did not terminate.\n");
#endif
        return {ImgBrowserStopStatus::TimedOut, result.win32_error};
    case PreloadSchedulerStopStatus::WaitFailed:
#if defined(IMGVW_DEBUG)
        WCHAR buf[256];
        swprintf_s(buf, L"ImgBrowser::StopTargetQueueing: WaitForSingleObject failed with error 0x%08lX\n",
                   static_cast<unsigned long>(result.win32_error));
        OutputDebugString(buf);
#endif
        return {ImgBrowserStopStatus::WaitFailed, result.win32_error};
    }

    return {ImgBrowserStopStatus::WaitFailed, ERROR_INVALID_FUNCTION};
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

void ImgBrowserCore::Reset(ULONG generation)
{
    session_.Reset(generation);
    preload_scheduler_.Reset();
}

BOOL ImgBrowserCore::AddPreloadTargetSize(INT targetwidth, INT targetheight)
{
    return preload_scheduler_.AddTargetSize(targetwidth, targetheight) ? TRUE : FALSE;
}

BOOL ImgBrowserCore::AddPreloadTargetSizes(const std::vector<SIZE>& target_sizes, std::vector<TargetSize>* added_sizes)
{
    return preload_scheduler_.AddTargetSizes(target_sizes, added_sizes) ? TRUE : FALSE;
}

BOOL ImgBrowserCore::IsTargetSizeActiveLocked(INT targetwidth, INT targetheight) const
{
    return preload_scheduler_.IsTargetSizeActive(targetwidth, targetheight) ? TRUE : FALSE;
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

        const auto imgformat = ImgFormatResolver::Resolve(filepath);
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
                const auto result = loadcontext_->loader->QueueItem(imgitem, next, loadcontext_->CurrentGeneration());
                if (result.Accepted())
                {
                    next = FALSE;
                    NotifyChanged();
                }
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
        const HANDLE events[] = {cancellation->event(), test_hooks_->path_queue_continue};
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
    preload_scheduler_.Queue(
        [browser = shared_from_this(), target_sizes, loadnext](const std::shared_ptr<CancellationState>& cancellation) {
            browser->QueueTargetSizes(cancellation, target_sizes, loadnext);
        });
}

void ImgBrowserCore::QueuePathsAsync(std::vector<std::wstring> paths, INT targetwidth, INT targetheight)
{
    preload_scheduler_.Queue([browser = shared_from_this(), paths = std::move(paths), targetwidth,
                              targetheight](const std::shared_ptr<CancellationState>& cancellation) {
        browser->QueuePaths(cancellation, paths, targetwidth, targetheight);
    });
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

        const auto imgformat = ImgFormatResolver::Resolve(filepath);
        EnterCriticalSection(browsecriticalsection_.get());
        const auto active = IsTargetSizeActiveLocked(targetwidth, targetheight);
        const auto imgitem =
            active ? GetOrCreateCachedItem(filepath, targetwidth, targetheight, imgformat) : std::shared_ptr<ImgItem>();
        LeaveCriticalSection(browsecriticalsection_.get());
        if (imgitem != nullptr)
        {
            const auto result = loadcontext_->loader->QueueItem(imgitem, FALSE, loadcontext_->CurrentGeneration());
            if (result.Accepted())
            {
                NotifyChanged();
            }
        }
    }
}

void ImgBrowserCore::QueueFileForTargetSizes(const std::wstring& filepath, ImgItem::Format imgformat, BOOL loadnext)
{
    BOOL next = loadnext;
    const auto currentitem = GetOrCreateCachedItem(filepath, targetwidth_, targetheight_, imgformat);
    if (currentitem != nullptr)
    {
        const auto result = loadcontext_->loader->QueueItem(currentitem, next, loadcontext_->CurrentGeneration());
        if (result.Accepted())
        {
            next = FALSE;
        }
    }

    for (const auto& target_size : preload_scheduler_.target_sizes())
    {
        if (target_size.width == targetwidth_ && target_size.height == targetheight_)
        {
            continue;
        }

        const auto imgitem = GetOrCreateCachedItem(filepath, target_size.width, target_size.height, imgformat);
        if (imgitem != nullptr)
        {
            const auto result = loadcontext_->loader->QueueItem(imgitem, next, loadcontext_->CurrentGeneration());
            if (result.Accepted())
            {
                next = FALSE;
            }
        }
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
    const auto imgformat = ImgFormatResolver::Resolve(filepath);
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
    FolderScanner scanner;
    const auto result =
        scanner.ScanFolders(folders, {[request] { return request->collection->cancellation->Cancelled(); },
                                      [this, request](const std::wstring& filepath, ImgItem::Format format) {
                                          CollectFile(request, filepath, format);
                                      },
                                      {}});
    InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(result.win32_error), ERROR_SUCCESS);

    EnterCriticalSection(browsecriticalsection_.get());
    folders_.clear();
    LeaveCriticalSection(browsecriticalsection_.get());
}

std::shared_ptr<ImgItem> ImgBrowserCore::GetOrCreateCachedItem(const std::wstring& filepath)
{
    const auto imgformat = ImgFormatResolver::Resolve(filepath);
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
        generation = session_.generation();
    }
    if (notificationhwnd_ != nullptr && notificationmessage_ != 0)
    {
        BOOL posted{};
#if defined(IMGVW_TESTING)
        if (test_hooks_ != nullptr && test_hooks_->fail_notification_post)
        {
            SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        }
        else
#endif
        {
            posted = PostMessage(notificationhwnd_, notificationmessage_, generation,
                                 static_cast<LPARAM>(ImgNotificationKind::BrowserState));
        }
        if (!posted)
        {
            const auto error = GetLastError();
            const auto current_generation = session_.generation();
            if (generation == current_generation)
            {
                session_.SetCollectionErrorIfClear(error);
            }
        }
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

    if (request->collection->subfolders_only)
    {
        request->browser->CollectSubFolders(request);
    }
    else
    {
        FolderScanner scanner;
        const auto recursive = InterlockedCompareExchange(&request->collection->recursive, 0, 0) != 0;
        const auto result =
            scanner.Scan(request->collection->folderpath, recursive,
                         {[request] { return request->collection->cancellation->Cancelled(); },
                          [browser = request->browser, request](const std::wstring& filepath, ImgItem::Format format) {
                              browser->CollectFile(request, filepath, format);
                          },
                          [browser = request->browser](const std::wstring& folderpath) {
                              EnterCriticalSection(browser->browsecriticalsection_.get());
                              browser->folders_.push_back(folderpath);
                              LeaveCriticalSection(browser->browsecriticalsection_.get());
                          }});
        InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(result.win32_error), ERROR_SUCCESS);
        if (recursive)
        {
            request->browser->CollectSubFolders(request);
        }
    }

    if (!request->browser->session_.SignalReadyEvent())
    {
        InterlockedCompareExchange(&request->enumeration_error, static_cast<LONG>(GetLastError()), ERROR_SUCCESS);
    }
    EnterCriticalSection(request->browser->browsecriticalsection_.get());
    if (request->collection->generation == request->browser->session_.generation())
    {
        request->browser->session_.SetCollectionError(static_cast<DWORD>(request->enumeration_error));
    }
    LeaveCriticalSection(request->browser->browsecriticalsection_.get());
    request->browser->NotifyChanged(request->collection->generation);

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
    return StartBrowseAsync(path, targetwidth, targetheight, clearloadcontext).Started() ? TRUE : FALSE;
}

ImgBrowserStartResult ImgBrowser::StartBrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight,
                                                   BOOL clearloadcontext)
{
    return core_->StartBrowseAsync(path, targetwidth, targetheight, clearloadcontext);
}

BOOL ImgBrowser::UpdateTargetSize(INT targetwidth, INT targetheight)
{
    return core_->UpdateTargetSize(targetwidth, targetheight);
}

BOOL ImgBrowser::BrowseSubFoldersAsync()
{
    return StartBrowseSubFoldersAsync().Started() ? TRUE : FALSE;
}

ImgBrowserStartResult ImgBrowser::StartBrowseSubFoldersAsync()
{
    return core_->StartBrowseSubFoldersAsync();
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

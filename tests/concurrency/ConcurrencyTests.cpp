#include "BrowsePath.h"
#include "BrowseSession.h"
#include "FolderScanner.h"
#include "ImgBrowser.h"
#include "ImgLoader.h"
#include "PreloadScheduler.h"
#include "Win32Handle.h"
#include "../support/JpegFixture.h"
#include "../support/TempFile.h"
#include "../support/TestHarness.h"

#include <Windows.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
BOOL WINAPI FailWorkEventSignal(HANDLE)
{
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
}

BOOL WINAPI FailNotificationPost(HWND, UINT, WPARAM, LPARAM)
{
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
}

class BlockingImgItem final : public ImgItem
{
  public:
    BlockingImgItem(std::wstring filepath, INT targetwidth, INT targetheight)
        : ImgItem(std::move(filepath), targetwidth, targetheight), started_(CreateEvent(nullptr, TRUE, FALSE, nullptr)),
          release_(CreateEvent(nullptr, TRUE, FALSE, nullptr))
    {
    }
    void Load() override
    {
        SetStatus(Status::Loading);
        SetEvent(started_.get());
        WaitForSingleObject(release_.get(), INFINITE);
        SetStatus(Status::Ready);
        SignalLoadComplete();
    }
    bool WaitUntilStarted(DWORD timeout_milliseconds = 2000) const
    {
        return WaitForSingleObject(started_.get(), timeout_milliseconds) == WAIT_OBJECT_0;
    }
    void Release()
    {
        SetEvent(release_.get());
    }

  private:
    Win32Handle started_;
    Win32Handle release_;
};

void TestFolderScanner()
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    const auto folder = TempPath((L"ImgVwFolderScanner-" + suffix).c_str());
    const auto child = folder + L"\\child";
    const auto top_level_image = folder + L"\\top.jpg";
    const auto child_image = child + L"\\nested.jpg";
    const auto unsupported_file = folder + L"\\ignored.txt";
    Check(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "folder scanner test folder is created");
    Check(CreateDirectoryW(child.c_str(), nullptr) != FALSE, "folder scanner test child folder is created");
    const auto jpeg = CreateJpeg(false, false);
    WriteBytes(top_level_image, jpeg);
    WriteBytes(child_image, jpeg);
    WriteBytes(unsupported_file, {0});

    FolderScanner scanner;
    std::vector<std::wstring> files;
    std::vector<std::wstring> folders;
    const auto flat_result =
        scanner.Scan(folder + L"\\", false,
                     {{},
                      [&files](const std::wstring& path, ImgItem::Format) { files.push_back(path); },
                      [&folders](const std::wstring& path) { folders.push_back(path); }});
    Check(flat_result.Succeeded(), "folder scanner completes a readable folder");
    Check(files.size() == 1 && files.front() == top_level_image,
          "folder scanner filters unsupported files from a flat scan");
    Check(folders.size() == 1 && folders.front() == child + L"\\",
          "folder scanner reports child folders for a flat scan");

    files.clear();
    const auto recursive_result = scanner.Scan(
        folder + L"\\", true, {{}, [&files](const std::wstring& path, ImgItem::Format) { files.push_back(path); }, {}});
    Check(recursive_result.Succeeded() && files.size() == 2,
          "folder scanner includes supported files from recursive children");

    bool cancelled = false;
    files.clear();
    const auto cancelled_result = scanner.Scan(folder + L"\\", true,
                                               {[&cancelled] { return cancelled; },
                                                [&files, &cancelled](const std::wstring& path, ImgItem::Format) {
                                                    files.push_back(path);
                                                    cancelled = true;
                                                },
                                                {}});
    Check(cancelled_result.Cancelled() && cancelled_result.win32_error == ERROR_SUCCESS && files.size() == 1,
          "folder scanner reports callback cancellation without inventing an enumeration error");

    const auto missing_result = scanner.Scan(folder + L"\\missing\\", false, {});
    Check(missing_result.status == FolderScanStatus::EnumerationFailed && missing_result.win32_error != ERROR_SUCCESS,
          "folder scanner preserves an enumeration failure distinctly from cancellation");

    DeleteFileW(top_level_image.c_str());
    DeleteFileW(child_image.c_str());
    DeleteFileW(unsupported_file.c_str());
    RemoveDirectoryW(child.c_str());
    RemoveDirectoryW(folder.c_str());
}

void TestBrowseSession()
{
    BrowseSession session;
    const auto generation = NextImgGeneration();
    session.set_folderpath(L"C:\\Images\\");
    session.files().Add(L"C:\\Images\\first.jpg");
    session.folders().push_back(L"C:\\Images\\child\\");

    Check(session.folderpath() == L"C:\\Images\\" && session.files().Size() == 1 && session.folders().size() == 1,
          "browse session owns the current folder and discovered navigation state");

    session.Reset(generation);
    session.SetCollectionErrorIfClear(ERROR_ACCESS_DENIED);
    session.SetCollectionErrorIfClear(ERROR_WRITE_FAULT);
    Check(session.folderpath().empty() && session.files().Empty() && session.folders().empty() &&
              session.generation() == generation && session.collection_error() == ERROR_ACCESS_DENIED,
          "browse session reset advances collection state and preserves its first collection error");
}

void TestPreloadScheduler()
{
    PreloadScheduler scheduler;
    scheduler.SetActiveTargetSize(800, 600);
    std::vector<PreloadSize> added_sizes;
    const std::vector<SIZE> requested{{800, 600}, {1920, 1080}, {1920, 1080}, {0, 720}};

    Check(scheduler.AddTargetSizes(requested, &added_sizes) && added_sizes.size() == 2,
          "preload scheduler accepts unique positive target sizes");
    Check(scheduler.IsTargetSizeActive(800, 600) && scheduler.IsTargetSizeActive(1920, 1080),
          "preload scheduler keeps the active and requested target sizes available");
    Check(!scheduler.AddTargetSize(1920, 1080), "preload scheduler rejects duplicate target sizes");

    scheduler.Reset();
    Check(scheduler.target_sizes().empty() && scheduler.IsTargetSizeActive(800, 600),
          "preload scheduler reset preserves the current active target size");

    Win32Handle started(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    Check(started.valid(), "preload scheduler test creates a worker-start event");
    Check(scheduler.Queue([event = started.get()](const std::shared_ptr<BrowseSessionCancellation>& cancellation) {
              SetEvent(event);
              WaitForSingleObject(cancellation->event(), INFINITE);
          }),
          "preload scheduler starts worker-owned queue work");
    Check(WaitForSingleObject(started.get(), 3000) == WAIT_OBJECT_0, "preload scheduler worker starts");
    const auto stop = scheduler.Stop(3000);
    Check(stop.Stopped(), "preload scheduler cancels and joins queued work");
}

void TestBrowserStartResults()
{
    const auto invalid_target = ImgBrowser().StartBrowseAsync(L".", 0, 480);
    Check(invalid_target.status == ImgBrowserStartStatus::InvalidTargetSize &&
              invalid_target.win32_error == ERROR_INVALID_PARAMETER,
          "browser start reports an invalid target size");

    const auto missing_path = TempPath(L"ImgVwMissingBrowseFolder");
    RemoveDirectoryW(missing_path.c_str());
    const auto classified_missing_path = ClassifyBrowsePath(missing_path);
    const auto invalid_path = ImgBrowser().StartBrowseAsync(missing_path, 640, 480);
    Check(invalid_path.status == ImgBrowserStartStatus::InvalidPath &&
              invalid_path.win32_error == classified_missing_path.win32_error &&
              invalid_path.win32_error != ERROR_SUCCESS,
          "browser start preserves path classification failures");

    const auto folder = TempPath(L"ImgVwBrowserStartResults");
    CreateDirectoryW(folder.c_str(), nullptr);

    auto reset_hooks = std::make_shared<ImgBrowserTestHooks>();
    reset_hooks->fail_ready_reset = true;
    ImgBrowser reset_browser(reset_hooks);
    const auto reset_failure = reset_browser.StartBrowseAsync(folder, 640, 480);
    Check(reset_failure.status == ImgBrowserStartStatus::ResetReadyEventFailed &&
              reset_failure.win32_error == ERROR_WRITE_FAULT,
          "browser start preserves ready-event reset failures");

    auto thread_hooks = std::make_shared<ImgBrowserTestHooks>();
    thread_hooks->fail_collector_thread_create = true;
    ImgBrowser thread_browser(thread_hooks);
    const auto thread_failure = thread_browser.StartBrowseAsync(folder, 640, 480);
    Check(thread_failure.status == ImgBrowserStartStatus::CreateThreadFailed &&
              thread_failure.win32_error == ERROR_NOT_ENOUGH_MEMORY,
          "browser start preserves collector thread creation failures");

    auto wait_hooks = std::make_shared<ImgBrowserTestHooks>();
    wait_hooks->fail_collector_wait = true;
    ImgBrowser wait_browser(wait_hooks);
    const auto wait_failure = wait_browser.StartBrowseSubFoldersAsync();
    Check(wait_failure.status == ImgBrowserStartStatus::WaitFailed &&
              wait_failure.win32_error == ERROR_INVALID_HANDLE,
          "recursive browser start preserves collector wait failures");

    RemoveDirectoryW(folder.c_str());
}

void TestLoaderShutdown()
{
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        ImgLoader loader;
        Check(loader.start_result().Started(), "loader starts its controller thread");
        Check(loader.StopLoading().status == ImgLoaderStopStatus::Stopped, "idle loader stops");
        Check(loader.StopLoading().status == ImgLoaderStopStatus::AlreadyStopped, "loader stop is idempotent");
        auto rejected = std::make_shared<BlockingImgItem>(L"rejected.png", 800, 600);
        const auto queue_result = loader.QueueItem(rejected);
        Check(queue_result.status == ImgLoaderQueueStatus::Stopping,
              "stopped loader reports that queueing is no longer available");
        Check(loader.GetStats().queued == 0 && !rejected->WaitUntilStarted(0), "stopped loader rejects new work");
    }
}

void TestLoaderQueueSignalFailure()
{
    ImgLoader loader(FailWorkEventSignal);
    auto item = std::make_shared<BlockingImgItem>(L"signal-failure.png", 800, 600);
    const auto result = loader.QueueItem(item);
    Check(result.status == ImgLoaderQueueStatus::SignalFailed && result.win32_error == ERROR_WRITE_FAULT,
          "loader queue preserves a work-event signal failure");
    Check(loader.GetStats().queued == 0 && !item->WaitUntilStarted(0),
          "loader rolls back queue bookkeeping after a signal failure");
    item->Release();
    Check(loader.StopLoading(2000).Stopped(), "loader stops after a queue signal failure");
}

void TestLoaderNotificationFailure()
{
    ImgLoader loader(SetEvent, FailNotificationPost);
    loader.SetNotificationWindow(GetDesktopWindow(), WM_APP + 2);
    auto item = std::make_shared<BlockingImgItem>(L"notification-failure.png", 800, 600);
    Check(loader.QueueItem(item).Accepted(), "notification failure test item is queued");
    Check(item->WaitUntilStarted(), "notification failure test item starts");
    item->Release();

    const auto started = GetTickCount();
    auto stats = loader.GetStats();
    while (stats.notification_failures == 0 && GetTickCount() - started < 2000)
    {
        Sleep(1);
        stats = loader.GetStats();
    }
    Check(stats.notification_failures == 1 && stats.notification_error == ERROR_INVALID_WINDOW_HANDLE,
          "loader stats preserve an asynchronous notification failure");
    Check(loader.StopLoading(2000).Stopped(), "loader stops after a notification failure");
}

void TestLoaderActiveWorkerTimeout()
{
    ImgLoader loader;
    auto item = std::make_shared<BlockingImgItem>(L"blocking.png", 800, 600);
    loader.QueueItem(item);
    Check(item->WaitUntilStarted(), "blocking loader item starts");

    const auto timeout = loader.StopLoading(0);
    Check(timeout.status == ImgLoaderStopStatus::TimedOut, "active loader reports a bounded stop timeout");

    item->Release();
    Check(WaitForSingleObject(item->loadedevent(), 2000) == WAIT_OBJECT_0,
          "timed-out loader worker retains valid state through completion");
    Check(loader.StopLoading(2000).Stopped(), "loader stop succeeds after active work completes");
}

void TestLoaderDiscardsQueuedItems()
{
    ImgLoader loader;
    auto first = std::make_shared<BlockingImgItem>(L"first.png", 800, 600);
    auto second = std::make_shared<BlockingImgItem>(L"second.png", 800, 600);
    auto queued_first = std::make_shared<BlockingImgItem>(L"queued-first.png", 800, 600);
    auto queued_second = std::make_shared<BlockingImgItem>(L"queued-second.png", 800, 600);

    loader.QueueItem(first);
    loader.QueueItem(second);
    Check(first->WaitUntilStarted() && second->WaitUntilStarted(), "both loader slots become active");
    const auto queued_result = loader.QueueItem(queued_first);
    Check(queued_result.status == ImgLoaderQueueStatus::Queued, "loader reports newly queued work");
    Check(loader.QueueItem(queued_first).status == ImgLoaderQueueStatus::AlreadyPending,
          "loader reports duplicate pending work");
    Check(loader.QueueItem(queued_first, TRUE).status == ImgLoaderQueueStatus::Reprioritized,
          "loader reports queued work reprioritization");
    loader.QueueItem(queued_second);
    Check(loader.GetStats().queued >= 1, "blocked loader retains queued test items");

    loader.DiscardQueuedItems();
    Check(loader.GetStats().queued == 0, "discard removes queued loader items");

    Check(loader.StopLoading(0).status == ImgLoaderStopStatus::TimedOut, "blocked loader stop remains bounded");
    Check(!queued_first->WaitUntilStarted(0) && !queued_second->WaitUntilStarted(0),
          "loader cancellation does not dispatch queued work");
    first->Release();
    second->Release();
    queued_first->Release();
    queued_second->Release();
    Check(loader.StopLoading(2000).Stopped(), "discard test loader stops after workers are released");
}

void TestLoaderDiscardsQueuedItemsForTargetSize()
{
    ImgLoader loader;
    auto active_first = std::make_shared<BlockingImgItem>(L"active-first.png", 800, 600);
    auto active_second = std::make_shared<BlockingImgItem>(L"active-second.png", 800, 600);
    auto first = std::make_shared<BlockingImgItem>(L"first.png", 800, 600);
    auto second = std::make_shared<BlockingImgItem>(L"second.png", 800, 600);
    auto other = std::make_shared<BlockingImgItem>(L"other.png", 1920, 1080);

    loader.QueueItem(active_first);
    loader.QueueItem(active_second);
    Check(active_first->WaitUntilStarted() && active_second->WaitUntilStarted(),
          "target discard fills both loader slots");
    loader.QueueItem(first);
    loader.QueueItem(second);
    loader.QueueItem(other);

    loader.DiscardQueuedItemsForTargetSize(800, 600);
    Check(other->status() == ImgItem::Status::Queued && !other->WaitUntilStarted(0),
          "target-size discard preserves undispatched work for other sizes");

    Check(loader.StopLoading(0).status == ImgLoaderStopStatus::TimedOut,
          "target discard loader reports active-worker timeout");
    Check(!first->WaitUntilStarted(0) && !second->WaitUntilStarted(0) && !other->WaitUntilStarted(0),
          "target discard cancellation leaves pending work undispatched");
    active_first->Release();
    active_second->Release();
    first->Release();
    second->Release();
    other->Release();
    Check(loader.StopLoading(2000).Stopped(), "target discard loader stops after workers are released");
}

void TestLoaderDispatchesTargetSizesRoundRobin()
{
    ImgLoader loader;
    auto active_first = std::make_shared<BlockingImgItem>(L"active-first.png", 800, 600);
    auto active_second = std::make_shared<BlockingImgItem>(L"active-second.png", 800, 600);
    loader.QueueItem(active_first);
    loader.QueueItem(active_second);
    Check(active_first->WaitUntilStarted() && active_second->WaitUntilStarted(),
          "round-robin test fills both loader slots");

    loader.PrioritizeTargetSize(800, 600);
    auto primary_first = std::make_shared<BlockingImgItem>(L"primary-first.png", 800, 600);
    auto primary_second = std::make_shared<BlockingImgItem>(L"primary-second.png", 800, 600);
    auto secondary_first = std::make_shared<BlockingImgItem>(L"secondary-first.png", 1920, 1080);
    auto secondary_second = std::make_shared<BlockingImgItem>(L"secondary-second.png", 1920, 1080);
    loader.QueueItem(primary_first);
    loader.QueueItem(primary_second);
    loader.QueueItem(secondary_first);
    loader.QueueItem(secondary_second);
    Check(loader.GetStats().queued == 4, "loader does not reserve queued work before a slot is available");

    active_first->Release();
    Check(secondary_first->WaitUntilStarted(), "new secondary resolution receives the next available slot");
    Check(!primary_first->WaitUntilStarted(0), "primary resolution waits for its round-robin turn");

    active_second->Release();
    Check(primary_first->WaitUntilStarted(), "primary resolution receives the following slot");

    secondary_first->Release();
    Check(secondary_second->WaitUntilStarted(), "secondary resolution receives its second round-robin turn");

    primary_first->Release();
    Check(primary_second->WaitUntilStarted(), "primary resolution receives its second round-robin turn");

    secondary_second->Release();
    primary_second->Release();
    Check(loader.StopLoading(2000).Stopped(), "round-robin loader stops after all workers are released");
}

bool WaitForBrowserCollection(ImgBrowser& browser, DWORD timeout_milliseconds = 3000)
{
    const auto started = GetTickCount();
    while (!browser.IsCollectingComplete() && GetTickCount() - started < timeout_milliseconds)
    {
        Sleep(1);
    }
    return browser.IsCollectingComplete() != FALSE;
}

bool WaitForBrowserError(ImgBrowser& browser, DWORD expected_error, DWORD timeout_milliseconds = 3000)
{
    const auto started = GetTickCount();
    while (browser.GetCollectionError() != expected_error && GetTickCount() - started < timeout_milliseconds)
    {
        Sleep(1);
    }
    return browser.GetCollectionError() == expected_error;
}

bool WaitForCachedImages(const std::shared_ptr<ImgBrowserLoadContext>& context,
                         const std::vector<std::wstring>& paths, INT targetwidth, INT targetheight,
                         DWORD timeout_milliseconds = 5000)
{
    const auto started = GetTickCount();
    for (const auto& path : paths)
    {
        std::shared_ptr<ImgItem> item;
        while (item == nullptr && GetTickCount() - started < timeout_milliseconds)
        {
            item = context->cache->Get(path, targetwidth, targetheight);
            if (item == nullptr)
            {
                Sleep(1);
            }
        }
        if (item == nullptr)
        {
            return false;
        }

        const auto elapsed = GetTickCount() - started;
        const auto remaining = elapsed < timeout_milliseconds ? timeout_milliseconds - elapsed : 0;
        if (WaitForSingleObject(item->loadedevent(), remaining) != WAIT_OBJECT_0 ||
            item->status() != ImgItem::Status::Ready)
        {
            return false;
        }
    }
    return true;
}

void DrainWindowMessage(HWND window, UINT message)
{
    MSG pending{};
    while (PeekMessageW(&pending, window, message, message, PM_REMOVE))
    {
    }
}

bool WaitForWindowMessage(HWND window, UINT message, MSG* received_message = nullptr,
                          DWORD timeout_milliseconds = 3000)
{
    const auto started = GetTickCount();
    MSG pending{};
    while (GetTickCount() - started < timeout_milliseconds)
    {
        if (PeekMessageW(&pending, window, message, message, PM_REMOVE))
        {
            if (received_message != nullptr)
            {
                *received_message = pending;
            }
            return true;
        }
        Sleep(1);
    }
    return false;
}

void TestBrowserNotificationGenerations()
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    const auto folder = TempPath((L"ImgVwGeneration-" + suffix).c_str());
    Check(CreateDirectoryW(folder.c_str(), nullptr) != FALSE, "generation test folder is created");
    const auto notification_window =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    Check(notification_window != nullptr, "generation test notification window is created");

    ImgBrowser browser;
    browser.SetNotificationWindow(notification_window, WM_APP + 3);
    const auto previous_generation = browser.generation();
    Check(browser.BrowseAsync(folder, 640, 480), "generation test browsing starts");
    Check(WaitForBrowserCollection(browser), "generation test collection completes");

    MSG notification{};
    Check(WaitForWindowMessage(notification_window, WM_APP + 3, &notification),
          "collection publishes a generation-bearing notification");
    Check(notification.wParam == browser.generation() &&
              notification.lParam == static_cast<LPARAM>(ImgNotificationKind::BrowserState),
          "collection notification carries the current browser generation");
    Check(!browser.IsCurrentNotification(previous_generation,
                                         static_cast<LPARAM>(ImgNotificationKind::BrowserState)),
          "browser rejects a notification from the previous session");
    ImgBrowser other_browser;
    Check(other_browser.generation() != browser.generation() &&
              other_browser.loadgeneration() != browser.loadgeneration(),
          "browser and load generations are unique across browser instances");

    DrainWindowMessage(notification_window, WM_APP + 3);
    const auto load_generation = browser.loadgeneration();
    auto active = std::make_shared<BlockingImgItem>(L"generation-active.png", 640, 480);
    browser.loadcontext()->loader->QueueItem(active, FALSE, load_generation);
    Check(active->WaitUntilStarted(), "generation test worker starts");
    browser.loadcontext()->Clear();
    Check(browser.loadgeneration() != load_generation, "clearing the load context advances its generation");
    active->Release();
    Check(WaitForWindowMessage(notification_window, WM_APP + 3, &notification),
          "old worker completion publishes its original generation");
    Check(notification.wParam == load_generation &&
              notification.lParam == static_cast<LPARAM>(ImgNotificationKind::LoadComplete),
          "worker completion retains the generation captured when queued");
    Check(!browser.IsCurrentNotification(notification.wParam, notification.lParam),
          "browser rejects a completion from the cleared load context");

    DestroyWindow(notification_window);
    RemoveDirectoryW(folder.c_str());
}

void TestBrowserNotificationFailure()
{
    const auto folder = TempPath(L"ImgVwBrowserNotificationFailure");
    CreateDirectoryW(folder.c_str(), nullptr);
    auto hooks = std::make_shared<ImgBrowserTestHooks>();
    hooks->fail_notification_post = true;
    ImgBrowser browser(hooks);
    browser.SetNotificationWindow(GetDesktopWindow(), WM_APP + 4);

    Check(browser.BrowseAsync(folder, 640, 480), "notification failure browser starts");
    Check(WaitForBrowserCollection(browser), "notification failure browser collection completes");
    Check(WaitForBrowserError(browser, ERROR_INVALID_WINDOW_HANDLE),
          "browser preserves the current generation's notification failure");

    RemoveDirectoryW(folder.c_str());
}

void TestRandomToSequentialMultiMonitorPreloadContextReuse()
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    const auto source_folder = TempPath((L"ImgVwPreloadSource-" + suffix).c_str());
    const auto target_folder = TempPath((L"ImgVwPreloadTarget-" + suffix).c_str());
    Check(CreateDirectoryW(source_folder.c_str(), nullptr) != FALSE, "preload source folder is created");
    Check(CreateDirectoryW(target_folder.c_str(), nullptr) != FALSE, "preload target folder is created");

    const auto jpeg = CreateJpeg(false, false);
    std::vector<std::wstring> paths;
    for (int index = 0; index < 6; ++index)
    {
        const auto path = source_folder + L"\\image-" + std::to_wstring(index) + L".jpg";
        WriteBytes(path, jpeg);
        paths.push_back(path);
    }

    struct TargetSession
    {
        INT width{};
        INT height{};
        std::shared_ptr<ImgBrowserLoadContext> context;
    };
    std::vector<TargetSession> targets{{800, 600, {}}, {1280, 720, {}}};

    {
        ImgBrowser source;
        Check(source.BrowseAsync(source_folder, 640, 480), "preload source browsing starts");
        Check(WaitForBrowserCollection(source), "preload source collection completes");
        Check(source.GetStats().found_images == paths.size(), "preload source exposes every image path");
        source.BeginRandomCycle();
        Check(source.MoveToRandom(), "random multi-monitor preload selects its first slide");

        for (auto& target : targets)
        {
            Win32Handle entered(CreateEvent(nullptr, TRUE, FALSE, nullptr));
            Win32Handle resume(CreateEvent(nullptr, TRUE, FALSE, nullptr));
            Win32Handle resumed(CreateEvent(nullptr, TRUE, FALSE, nullptr));
            const auto hooks = std::make_shared<ImgBrowserTestHooks>();
            hooks->path_queue_entered = entered.get();
            hooks->path_queue_continue = resume.get();
            hooks->path_queue_resumed = resumed.get();

            const auto notification_window =
                CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
            Check(notification_window != nullptr, "preload target notification window is created");

            auto browser = std::make_unique<ImgBrowser>(hooks);
            browser->SetNotificationWindow(notification_window, WM_APP + 1);
            Check(browser->BrowseAsync(target_folder, target.width, target.height),
                  "preload target browsing starts");
            Check(WaitForBrowserCollection(*browser), "preload target collection completes");
            browser->PreloadFrom(source);
            Check(WaitForSingleObject(entered.get(), 3000) == WAIT_OBJECT_0,
                  "background path preloading reaches the deterministic pause");

            target.context = browser->loadcontext();
            browser.reset();
            Check(WaitForSingleObject(resumed.get(), 0) == WAIT_OBJECT_0,
                  "destroying a target browser cancels and joins its paused path queue");
            Check(target.context->loader->GetStats().notifications == 0,
                  "destroyed target browser removes its loader notification");
            DestroyWindow(notification_window);
        }

        source.MoveToLast();
        Check(source.MoveToFirst(), "sequential multi-monitor preload selects its first slide after the mode switch");

        for (auto& target : targets)
        {
            const auto notification_window =
                CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
            Check(notification_window != nullptr, "replacement target notification window is created");

            {
                ImgBrowser replacement;
                replacement.ShareLoadContext(target.context);
                replacement.SetNotificationWindow(notification_window, WM_APP + 2);
                Check(target.context->loader->GetStats().notifications == 1,
                      "reused load context retains the replacement browser notification");
                Check(replacement.BrowseAsync(target_folder, target.width, target.height),
                      "replacement target browsing starts");
                Check(WaitForBrowserCollection(replacement), "replacement target collection completes");
                DrainWindowMessage(notification_window, WM_APP + 2);

                auto active_first =
                    std::make_shared<BlockingImgItem>(L"secondary-active-first.png", target.width, target.height);
                auto active_second =
                    std::make_shared<BlockingImgItem>(L"secondary-active-second.png", target.width, target.height);
                target.context->loader->QueueItem(active_first);
                target.context->loader->QueueItem(active_second);
                Check(active_first->WaitUntilStarted() && active_second->WaitUntilStarted(),
                      "secondary preload test occupies every loader slot");

                replacement.PreloadFrom(source);
                Check(WaitForWindowMessage(notification_window, WM_APP + 2),
                      "secondary preload queue publishes a browser notification");
                Check(target.context->loader->GetStats().queued > 0,
                      "secondary preload queue is observable while loader slots are occupied");
                active_first->Release();
                active_second->Release();
                Check(WaitForCachedImages(target.context, paths, target.width, target.height),
                      "replacement target preloads every image into the reused context");
            }

            Check(target.context->loader->GetStats().notifications == 0,
                  "replacement browser removes its notification at teardown");
            DestroyWindow(notification_window);
        }
    }

    for (const auto& path : paths)
    {
        DeleteFileW(path.c_str());
    }
    RemoveDirectoryW(source_folder.c_str());
    RemoveDirectoryW(target_folder.c_str());
}

void TestMultiMonitorPreloadResynchronizesNewSourcePaths()
{
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    const auto source_folder = TempPath((L"ImgVwGrowingPreloadSource-" + suffix).c_str());
    const auto target_folder = TempPath((L"ImgVwGrowingPreloadTarget-" + suffix).c_str());
    Check(CreateDirectoryW(source_folder.c_str(), nullptr) != FALSE, "growing preload source folder is created");
    Check(CreateDirectoryW(target_folder.c_str(), nullptr) != FALSE, "growing preload target folder is created");

    const auto jpeg = CreateJpeg(false, false);
    std::vector<std::wstring> paths;
    for (int index = 0; index < 2; ++index)
    {
        const auto path = source_folder + L"\\image-" + std::to_wstring(index) + L".jpg";
        WriteBytes(path, jpeg);
        paths.push_back(path);
    }

    ImgBrowser source;
    ImgBrowser target;
    Check(source.BrowseAsync(source_folder, 640, 480), "growing preload source browsing starts");
    Check(target.BrowseAsync(target_folder, 1280, 720), "growing preload target browsing starts");
    Check(WaitForBrowserCollection(source) && WaitForBrowserCollection(target),
          "growing preload browser collections complete");
    Check(target.PreloadFrom(source) == paths.size(), "initial preload reports every collected source path");
    Check(WaitForCachedImages(target.loadcontext(), paths, 1280, 720), "initial preload caches every source path");

    const auto added_path = source_folder + L"\\image-2.jpg";
    WriteBytes(added_path, jpeg);
    paths.push_back(added_path);
    Check(source.MoveToOrAddItem(added_path), "source accepts a path discovered after initial preload");
    Check(target.PreloadFrom(source) == paths.size(), "resynchronized preload observes the expanded source");
    Check(WaitForCachedImages(target.loadcontext(), paths, 1280, 720),
          "resynchronized preload caches the newly discovered path");

    for (const auto& path : paths)
    {
        DeleteFileW(path.c_str());
    }
    RemoveDirectoryW(source_folder.c_str());
    RemoveDirectoryW(target_folder.c_str());
}

} // namespace

void RunConcurrencyTests()
{
    TestBrowseSession();
    TestPreloadScheduler();
    TestFolderScanner();
    TestBrowserStartResults();
    TestLoaderShutdown();
    TestLoaderQueueSignalFailure();
    TestLoaderNotificationFailure();
    TestLoaderActiveWorkerTimeout();
    TestLoaderDiscardsQueuedItems();
    TestLoaderDiscardsQueuedItemsForTargetSize();
    TestLoaderDispatchesTargetSizesRoundRobin();
    TestBrowserNotificationGenerations();
    TestBrowserNotificationFailure();
    TestRandomToSequentialMultiMonitorPreloadContextReuse();
    TestMultiMonitorPreloadResynchronizesNewSourcePaths();
}

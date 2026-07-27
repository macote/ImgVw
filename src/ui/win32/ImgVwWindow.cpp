#include "ImgVwWindow.h"

#include "BrowsePath.h"
#include "CompatibleDeviceContext.h"
#include "GdiObject.h"
#include "RegistryKey.h"
#include "SelectedGdiObject.h"
#include "WindowDeviceContext.h"

#include <Windowsx.h>

#include <algorithm>
#include <climits>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace
{
typedef UINT(WINAPI* GetDpiForWindowProc)(HWND hwnd);

constexpr UINT kImageCommandIds[] = {
    IDR_NEXT,   IDR_PREVIOUS, IDR_FIRST,          IDR_LAST,           IDR_RECYCLE,
    IDR_DELETE, IDR_TOGGLESS, IDR_TOGGLESSR,      IDR_TOGGLESS_MULTI, IDR_TOGGLESS_MULTI_RANDOM,
    IDR_INCSSS, IDR_DECSSS,   IDR_TOGGLE_FILENAME};

bool IsImageCommand(UINT command)
{
    for (const auto imagecommand : kImageCommandIds)
    {
        if (imagecommand == command)
        {
            return true;
        }
    }

    return false;
}

BOOL IsSystemLightTheme()
{
    RegistryKey personalize_key;
    const auto open_result =
        RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                     KEY_QUERY_VALUE, personalize_key.put());
    if (open_result != ERROR_SUCCESS)
    {
        // Windows XP has no AppsUseLightTheme preference. Keep its traditional light appearance.
        return TRUE;
    }

    DWORD value{};
    DWORD value_size = sizeof(value);
    const auto query_result = RegQueryValueEx(personalize_key.get(), L"AppsUseLightTheme", nullptr, nullptr,
                                              reinterpret_cast<LPBYTE>(&value), &value_size);
    return query_result == ERROR_SUCCESS ? (value != 0 ? TRUE : FALSE) : TRUE;
}

} // namespace

struct ImgVwWindow::MonitorCreateContext
{
    HINSTANCE hinst{};
    std::wstring path;
    HMONITOR primarymonitor{};
    ImgVwWindow* owner{};
    BOOL slideshowrandom{};
};

ImgVwWindow* ImgVwWindow::Create(HINSTANCE hInst, const std::vector<std::wstring>& args)
{
    auto self = new ImgVwWindow(hInst, args);
    if (self != nullptr)
    {
        self->backgroundbrush_.reset(CreateSolidBrush(RGB(0, 0, 0)));
        self->manualcursor_ = TRUE;
        self->dontfillbackground_ = TRUE;
        if (self->WinCreateWindow(WS_EX_APPWINDOW, L"ImgVw", WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN),
                                  GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr))
        {
            return self;
        }

        delete self;
    }

    return nullptr;
}

ImgVwWindow* ImgVwWindow::CreateOnMonitor(HINSTANCE hInst, const std::wstring& path, HMONITOR monitor,
                                          ImgVwWindow* owner)
{
    MONITORINFO monitorinfo{};
    monitorinfo.cbSize = sizeof(monitorinfo);
    if (!GetMonitorInfo(monitor, &monitorinfo))
    {
        return nullptr;
    }

    const auto width = monitorinfo.rcMonitor.right - monitorinfo.rcMonitor.left;
    const auto height = monitorinfo.rcMonitor.bottom - monitorinfo.rcMonitor.top;
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    auto self = new ImgVwWindow(hInst, path, owner, FALSE);
    if (self != nullptr)
    {
        const auto loadcontext =
            owner == nullptr ? std::shared_ptr<ImgBrowserLoadContext>() : owner->FindTargetLoadContext(width, height);
        if (loadcontext != nullptr)
        {
            // Preserve independent navigation while sharing decoded images and loader capacity for this target size.
            self->browser_.ShareLoadContext(loadcontext);
        }
        else if (owner != nullptr)
        {
            owner->RememberTargetLoadContext(width, height, self->browser_.loadcontext());
        }

        self->backgroundbrush_.reset(CreateSolidBrush(RGB(0, 0, 0)));
        self->manualcursor_ = TRUE;
        self->dontfillbackground_ = TRUE;
        const auto ownerhwnd = owner == nullptr ? nullptr : owner->hwnd();
        if (self->WinCreateWindow(WS_EX_TOOLWINDOW, L"ImgVw", WS_POPUP, monitorinfo.rcMonitor.left,
                                  monitorinfo.rcMonitor.top, width, height, ownerhwnd, nullptr))
        {
            self->monitor_placement_.SetCurrent(monitor);
            return self;
        }

        delete self;
    }

    return nullptr;
}

BOOL CALLBACK ImgVwWindow::CreateSlideShowWindowForMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
    auto context = reinterpret_cast<MonitorCreateContext*>(param);
    if (context == nullptr || monitor == context->primarymonitor)
    {
        return TRUE;
    }

    const auto window = CreateOnMonitor(context->hinst, context->path, monitor, context->owner);
    if (window != nullptr)
    {
        if (!context->owner->multi_monitor_slideshow_.RegisterSecondaryWindow(WindowId(window->hwnd())))
        {
            DestroyWindow(window->hwnd());
            return TRUE;
        }
        window->slideshow_.Start(context->slideshowrandom ? SlideShowMode::Random : SlideShowMode::Sequential);
        ShowWindow(window->hwnd(), SW_SHOWNOACTIVATE);
        if (context->owner->browse_state_.browse_subfolders())
        {
            window->EnableBrowseSubFolders();
        }
    }

    return TRUE;
}

LRESULT ImgVwWindow::OnCreate()
{
    QueryPerformanceFrequency(&qpcfrequency_);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (primarywindow_)
    {
        ApplyCursorCapture(true);
        ApplyCursorVisibility(cursor_controller_.SetVisible(false));
    }

    InitializeMonitorState();
    UpdateSystemTheme();
    if (owner_ == nullptr)
    {
        empty_state_view_.Initialize(hwnd_, hinst_);
        DragAcceptFiles(hwnd_, TRUE);
    }
    if (browse_state_.path().empty())
    {
        ShowEmptyState(L"Open an image or browse a folder to begin.", FALSE);
    }
    else if (!InitializeBrowser(browse_state_.path()))
    {
        ShowEmptyState(L"The selected path could not be opened.", FALSE);
    }

    return FALSE;
}

void ImgVwWindow::PaintContent(PAINTSTRUCT* pps)
{
    if (IsEmptyStateVisible())
    {
        empty_state_view_.Paint(pps->hdc, GetWindowDpi(), systemlighttheme_ != FALSE);
        return;
    }

    const auto imgitem = browser_.GetCurrentItem();
    const DisplaySnapshot snapshot{imgitem, browser_.GetCurrentFilePath(),
                                   imgitem != nullptr ? imgitem->GetDisplayState() : ImgItem::DisplayState{}};
    bool overlaydrawn = false;
    if (imgitem != nullptr)
    {
        RECT client_rectangle{};
        GetClientRect(hwnd_, &client_rectangle);
        const DisplayPresentationInput presentation_input{pps->hdc,
                                                          backgroundbrush_.get(),
                                                          client_rectangle,
                                                          slideshow_.waiting_for_image(),
                                                          display_session_.first_paint(),
                                                          IsInfoOverlayVisible() &&
                                                              !IsRectEmpty(&info_overlay_.rectangle()),
                                                          info_overlay_.rectangle()};
        const auto presentation = display_presenter_.Present(snapshot, presentation_input);
        if (presentation.presentation == DisplayPresentation::WaitingForImage)
        {
            if (IsInfoOverlayVisible())
            {
                DisplayFileInformation(pps->hdc, pps->rcPaint, imgitem.get(), browser_.GetCurrentFilePath());
            }
            else
            {
                DisplayLoadingProgress(pps->hdc, pps->rcPaint, imgitem.get(), browser_.GetCurrentFilePath());
            }
            return;
        }

        if (presentation.ShouldCompleteFirstPaint())
        {
            display_session_.CompleteFirstPaint();
        }

        if (DisplayPresenter::ShouldDrawOnlyOverlay(IsInfoOverlayVisible() != FALSE, snapshot.state.status,
                                                    info_overlay_.rectangle(), pps->rcPaint))
        {
            DrawLoaderStatsOverlay(pps->hdc, imgitem.get());
            return;
        }

        if (presentation.IsImageReady())
        {
            display_session_.MarkPainted(snapshot.path);
        }
        else
        {
            overlaydrawn = !IsInfoOverlayVisible() &&
                           DisplayLoadingProgress(pps->hdc, pps->rcPaint, imgitem.get(), browser_.GetCurrentFilePath());
            if (!overlaydrawn)
            {
                overlaydrawn =
                    DisplayFileInformation(pps->hdc, pps->rcPaint, imgitem.get(), browser_.GetCurrentFilePath());
            }
        }
    }

    if (IsInfoOverlayVisible() && !overlaydrawn)
    {
        DrawLoaderStatsOverlay(pps->hdc, imgitem.get());
    }
}

BOOL ImgVwWindow::InitializeBrowser(const std::wstring& path, BOOL clearloadcontext)
{
    RECT windowrectangle{};
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return FALSE;
    }

    browser_.SetNotificationWindow(hwnd_, kBrowserChangedMessage);
    UpdateClientSize(windowrectangle.right, windowrectangle.bottom);
    if (!browser_.BrowseAsync(path, windowrectangle.right, windowrectangle.bottom, clearloadcontext))
    {
        return FALSE;
    }
    browse_state_.MarkBrowserInitialized();
    return TRUE;
}

BOOL ImgVwWindow::OpenPath(const std::wstring& path)
{
    if (path.empty())
    {
        return FALSE;
    }

    const auto preserve_load_contexts = BrowsePathsShareFolder(browse_state_.path(), path);
    const auto was_empty = IsEmptyStateVisible();
    if (was_empty)
    {
        HideEmptyState();
    }
    StopMultiMonitorSlideShow();
    StopSlideShow();
    if (!InitializeBrowser(path, preserve_load_contexts ? FALSE : TRUE))
    {
        if (was_empty)
        {
            ShowEmptyState(L"The selected path could not be opened.", FALSE);
        }
        else
        {
            MessageBox(hwnd_, L"The selected path could not be opened.", L"ImgVw Error", MB_OK | MB_ICONERROR);
        }
        return FALSE;
    }

    if (!preserve_load_contexts)
    {
        multi_monitor_slideshow_.ClearLoadContexts();
    }
    browse_state_.OpenPath(path);
    InvalidateScreen();
    return TRUE;
}

void ImgVwWindow::OpenImage()
{
    if (IsSearchingSubfolders())
    {
        return;
    }

    const auto restore_viewer_input = !IsEmptyStateVisible();
    if (restore_viewer_input)
    {
        ApplyCursorCapture(false);
        ApplyCursorVisibility(cursor_controller_.SetVisible(true));
    }

    SelectPath(path_picker_.SelectImage(hwnd_));
    empty_state_view_.RestoreFocus(IDM_OPEN_IMAGE);

    if (restore_viewer_input && !IsEmptyStateVisible())
    {
        ApplyCursorCapture(true);
        ApplyCursorVisibility(cursor_controller_.SetVisible(false));
    }
}

void ImgVwWindow::OpenFolder()
{
    if (IsSearchingSubfolders())
    {
        return;
    }

    const auto restore_viewer_input = !IsEmptyStateVisible();
    if (restore_viewer_input)
    {
        ApplyCursorCapture(false);
        ApplyCursorVisibility(cursor_controller_.SetVisible(true));
    }

    SelectPath(path_picker_.SelectFolder(hwnd_));
    empty_state_view_.RestoreFocus(IDM_OPEN_FOLDER);

    if (restore_viewer_input && !IsEmptyStateVisible())
    {
        ApplyCursorCapture(true);
        ApplyCursorVisibility(cursor_controller_.SetVisible(false));
    }
}

void ImgVwWindow::ActivateEmptyStateButton()
{
    empty_state_view_.ActivateFocusedButton();
}

void ImgVwWindow::SelectPath(const PathPickerResult& result)
{
    if (result.status == PathPickerStatus::Cancelled)
    {
        return;
    }

    if (result.status == PathPickerStatus::Failed)
    {
        if (IsEmptyStateVisible())
        {
            ShowEmptyState(L"The file or folder picker could not be opened.", FALSE);
        }
        else
        {
            MessageBox(hwnd_, L"The file or folder picker could not be opened.", L"ImgVw Error", MB_OK | MB_ICONERROR);
        }
        return;
    }

    OpenPath(result.path);
}

void ImgVwWindow::HandleDroppedFiles(HDROP drop)
{
    if (IsSearchingSubfolders())
    {
        DragFinish(drop);
        return;
    }

    const auto count = DragQueryFile(drop, 0xFFFFFFFF, nullptr, 0);
    if (count != 1)
    {
        DragFinish(drop);
        MessageBox(hwnd_, L"Drop one image or folder at a time.", L"ImgVw", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const auto path_length = DragQueryFile(drop, 0, nullptr, 0);
    std::vector<wchar_t> path(path_length + 1);
    DragQueryFile(drop, 0, path.data(), static_cast<UINT>(path.size()));
    DragFinish(drop);
    OpenPath(path.data());
}

void ImgVwWindow::BrowseEmptyStateSubFolders()
{
    if (!browse_state_.CanStartSubfolderSearch())
    {
        return;
    }

    if (browser_.BrowseSubFoldersAsync())
    {
        browse_state_.EnableSubfolderSearch();
        for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
        {
            const auto window = ResolveSlideShowWindow(window_id);
            if (window != nullptr)
            {
                window->EnableBrowseSubFolders();
            }
        }
        ShowSearchingSubfoldersState();
    }
}

BOOL ImgVwWindow::IsEmptyStateVisible() const
{
    return empty_state_view_.IsVisible() ? TRUE : FALSE;
}

BOOL ImgVwWindow::IsSearchingSubfolders() const
{
    return empty_state_view_.IsSearchingSubfolders() ? TRUE : FALSE;
}

BOOL ImgVwWindow::HasImages()
{
    return browse_state_.browser_initialized() && browser_.HasFiles();
}

void ImgVwWindow::ShowEmptyState(const std::wstring& message, BOOL show_search_subfolders)
{
    if (owner_ != nullptr)
    {
        return;
    }

    const auto was_visible = IsEmptyStateVisible();
    empty_state_view_.Show(message, browse_state_.browser_initialized(), show_search_subfolders != FALSE);
    if (!was_visible)
    {
        KillTimer(hwnd_, IDT_HIDEMOUSE);
        cursor_controller_.CancelIdleTimer();
        ApplyCursorCapture(false);
        ApplyCursorVisibility(cursor_controller_.SetVisible(true));
    }

    empty_state_view_.UpdateLayout(GetWindowDpi());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImgVwWindow::ShowSearchingSubfoldersState()
{
    empty_state_view_.ShowSearchingSubfolders();
    empty_state_view_.UpdateLayout(GetWindowDpi());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImgVwWindow::HideEmptyState()
{
    if (!IsEmptyStateVisible())
    {
        return;
    }

    empty_state_view_.Hide();
    if (primarywindow_)
    {
        ApplyCursorCapture(true);
        ApplyCursorVisibility(cursor_controller_.SetVisible(false));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

BOOL ImgVwWindow::TranslateEmptyStateDialogMessage(MSG* message) const
{
    return empty_state_view_.TranslateDialogMessage(message);
}

BOOL ImgVwWindow::UpdateClientSize(INT width, INT height)
{
    if (width <= 0 || height <= 0)
    {
        return FALSE;
    }

    if (clientwidth_ == width && clientheight_ == height)
    {
        return FALSE;
    }

    if (clientwidth_ > 0 && clientheight_ > 0)
    {
        RememberTargetLoadContext(clientwidth_, clientheight_, browser_.loadcontext());
    }

    const auto target_context = FindTargetLoadContext(width, height);
    if (target_context != nullptr && target_context != browser_.loadcontext())
    {
        browser_.ShareLoadContext(target_context);
    }

    clientwidth_ = width;
    clientheight_ = height;
    const auto restorepath = display_session_.restore_path();
    if (!restorepath.empty())
    {
        browser_.MoveToItem(restorepath);
    }

    return browser_.UpdateTargetSize(width, height);
}

void ImgVwWindow::HandleSize(WPARAM wParam, LPARAM lParam)
{
    if (wParam == SIZE_MINIMIZED)
    {
        return;
    }

    const auto width = static_cast<INT>(LOWORD(lParam));
    const auto height = static_cast<INT>(HIWORD(lParam));
    if (UpdateClientSize(width, height))
    {
        InvalidateScreen();
    }
    empty_state_view_.UpdateLayout(GetWindowDpi());
}

void ImgVwWindow::HandleDpiChanged(LPARAM lParam)
{
    ResetLoaderStatsOverlayLayout();

    const auto suggestedrect = reinterpret_cast<RECT*>(lParam);
    const auto suggested_bounds =
        suggestedrect == nullptr ? WindowPlacementBounds{} : MonitorPlacement::FromRectangle(*suggestedrect);
    if (suggested_bounds.valid)
    {
        SetWindowPos(hwnd_, nullptr, suggested_bounds.x, suggested_bounds.y, suggested_bounds.width,
                     suggested_bounds.height, SWP_NOACTIVATE | SWP_NOZORDER);
    }

    RECT clientrect{};
    if (GetClientRect(hwnd_, &clientrect) && UpdateClientSize(clientrect.right, clientrect.bottom))
    {
        InvalidateScreen();
    }

    if (IsInfoOverlayVisible())
    {
        RefreshLoaderStatsOverlay();
    }
    empty_state_view_.UpdateLayout(GetWindowDpi());
}

void ImgVwWindow::InitializeMonitorState()
{
    monitor_placement_.SetCurrent(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST));
}

void ImgVwWindow::UpdateSystemTheme()
{
    const auto light_theme = IsSystemLightTheme();
    if (systemlighttheme_ == light_theme)
    {
        return;
    }

    systemlighttheme_ = light_theme;
    if (IsInfoOverlayVisible())
    {
        RefreshLoaderStatsOverlay();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImgVwWindow::HandleWindowPosChanged()
{
    const auto monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    const auto transition = monitor_placement_.OnMonitorChanged(monitor, window_drag_controller_.active());
    if (!transition.changed)
    {
        return;
    }

    ResetLoaderStatsOverlayLayout();
    if (transition.apply_bounds)
    {
        ApplyMonitorBounds(monitor);
    }
}

BOOL ImgVwWindow::ApplyMonitorBounds(HMONITOR monitor)
{
    MONITORINFO monitorinfo{};
    monitorinfo.cbSize = sizeof(monitorinfo);
    if (!GetMonitorInfo(monitor, &monitorinfo))
    {
        return FALSE;
    }

    const auto bounds = MonitorPlacement::FromRectangle(monitorinfo.rcMonitor);
    if (!bounds.valid)
    {
        return FALSE;
    }

    return SetWindowPos(hwnd_, nullptr, bounds.x, bounds.y, bounds.width, bounds.height, SWP_NOACTIVATE | SWP_NOZORDER);
}

BOOL ImgVwWindow::HasMultipleMonitors() const
{
    return GetSystemMetrics(SM_CMONITORS) > 1;
}

BOOL ImgVwWindow::BeginWindowDrag(LPARAM lParam)
{
    if (multi_monitor_slideshow_.running() || owner_ != nullptr || !HasMultipleMonitors())
    {
        return FALSE;
    }

    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (!ClientToScreen(hwnd_, &point))
    {
        return FALSE;
    }

    RECT window_rectangle{};
    if (!GetWindowRect(hwnd_, &window_rectangle) || !window_drag_controller_.Begin(point, window_rectangle))
    {
        return FALSE;
    }

    ApplyCursorVisibility(cursor_controller_.SetVisible(true));
    return TRUE;
}

BOOL ImgVwWindow::UpdateWindowDrag(WPARAM wParam, LPARAM lParam)
{
    if (!window_drag_controller_.active())
    {
        return FALSE;
    }

    if ((wParam & MK_LBUTTON) == 0)
    {
        FinishWindowDrag();
        return FALSE;
    }

    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (!ClientToScreen(hwnd_, &point))
    {
        return FALSE;
    }

    const auto position = window_drag_controller_.CalculatePosition(point);
    return SetWindowPos(hwnd_, nullptr, position.x, position.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void ImgVwWindow::EndWindowDrag()
{
    if (!window_drag_controller_.End())
    {
        return;
    }

    ApplyCursorVisibility(cursor_controller_.SetVisible(false));
}

void ImgVwWindow::FinishWindowDrag()
{
    if (!window_drag_controller_.active())
    {
        return;
    }

    const auto monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    EndWindowDrag();
    if (monitor != nullptr)
    {
        monitor_placement_.SetCurrent(monitor);
        ApplyMonitorBounds(monitor);
    }
}

bool ImgVwWindow::DisplayFileInformation(HDC dc, const RECT& paintrect, const ImgItem* item,
                                         const std::wstring& filepath)
{
    if (IsRectEmpty(&paintrect))
    {
        return false;
    }

    const auto overlaydrawn = IsInfoOverlayVisible();
    auto text = InfoOverlay::BuildItemText(item, filepath);
    RECT overlayrect{};
    if (info_overlay_.stats_visible())
    {
        text = BuildLoaderStatsOverlayText();
        overlayrect = info_overlay_.CalculateRectangle(dc, text, GetWindowDpi(), clientwidth_, clientheight_);
        info_overlay_.SetContent(text, overlayrect);
    }
    else
    {
        overlayrect = info_overlay_.CalculateRectangle(dc, text, GetWindowDpi(), clientwidth_, clientheight_);
    }

    const auto paintwidth = paintrect.right - paintrect.left;
    const auto paintheight = paintrect.bottom - paintrect.top;
    CompatibleDeviceContext memorydc(CreateCompatibleDC(dc));
    GdiObject<HBITMAP> bitmap(memorydc.valid() ? CreateCompatibleBitmap(dc, paintwidth, paintheight) : nullptr);
    if (!memorydc.valid() || !bitmap.valid())
    {
        FillRect(dc, &paintrect, backgroundbrush_.get());
        info_overlay_.Draw(dc, overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
        return overlaydrawn;
    }

    SelectedGdiObject bitmap_selection(memorydc.get(), bitmap.get());
    if (!bitmap_selection.valid())
    {
        FillRect(dc, &paintrect, backgroundbrush_.get());
        info_overlay_.Draw(dc, overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
        return overlaydrawn;
    }

    POINT previousorigin{};
    SetViewportOrgEx(memorydc.get(), -paintrect.left, -paintrect.top, &previousorigin);
    FillRect(memorydc.get(), &paintrect, backgroundbrush_.get());
    info_overlay_.Draw(memorydc.get(), overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
    SetViewportOrgEx(memorydc.get(), previousorigin.x, previousorigin.y, nullptr);

    BitBlt(dc, paintrect.left, paintrect.top, paintwidth, paintheight, memorydc.get(), 0, 0, SRCCOPY);
    return overlaydrawn;
}

bool ImgVwWindow::DisplayLoadingProgress(HDC dc, const RECT& paintrect, const ImgItem* item,
                                         const std::wstring& filepath)
{
    if (IsRectEmpty(&paintrect) || !info_overlay_.loading_progress_visible())
    {
        return false;
    }

    const auto text = InfoOverlay::BuildItemText(item, filepath);
    const auto overlayrect = info_overlay_.CalculateRectangle(dc, text, GetWindowDpi(), clientwidth_, clientheight_);
    FillRect(dc, &paintrect, backgroundbrush_.get());
    info_overlay_.Draw(dc, overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
    return true;
}

void ImgVwWindow::UpdateLoadingProgressOverlayTimer()
{
    const auto item = browser_.GetCurrentItem();
    const auto waiting =
        item != nullptr && item->status() != ImgItem::Status::Ready && item->status() != ImgItem::Status::Error;
    const InfoOverlayProgressInput input{
        waiting,        item != nullptr ? item->loadingprogresspercent() : 0, browser_.GetCurrentFilePath(),
        GetTickCount(), kLoadingProgressOverlayDebounceInMilliseconds,        IsInfoOverlayVisible() != FALSE};
    const auto actions = info_overlay_.UpdateLoadingProgress(input);
    if (actions.cancel_progress_timer)
    {
        KillTimer(hwnd_, kLoadingProgressOverlayTimer);
    }
    if (actions.arm_progress_timer)
    {
        SetTimer(hwnd_, kLoadingProgressOverlayTimer, kLoadingProgressOverlayIntervalInMilliseconds, nullptr);
    }
    if (actions.refresh_overlay)
    {
        RefreshLoaderStatsOverlay();
    }
    if (actions.invalidate_all)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

BOOL ImgVwWindow::IsLoaderStatsOverlayKeyDown() const
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0 && (GetKeyState(VK_MENU) & 0x8000) != 0;
}

BOOL ImgVwWindow::IsFilenameOverlayVisible() const
{
    const auto stateowner = owner_ == nullptr ? this : owner_;
    return stateowner->info_overlay_visibility_.filename_visible(IsLoaderStatsOverlayKeyDown() != FALSE);
}

BOOL ImgVwWindow::IsInfoOverlayVisible() const
{
    return info_overlay_.stats_visible() || IsFilenameOverlayVisible();
}

void ImgVwWindow::UpdateLoaderStatsOverlayVisibility()
{
    if (owner_ != nullptr)
    {
        owner_->UpdateLoaderStatsOverlayVisibility();
        return;
    }

    info_overlay_visibility_.OnStatsRequested(IsLoaderStatsOverlayKeyDown() != FALSE);

    UpdateLoaderStatsOverlayVisibilityForWindow();
    for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
    {
        const auto window = ResolveSlideShowWindow(window_id);
        if (window != nullptr)
        {
            window->UpdateLoaderStatsOverlayVisibilityForWindow();
        }
    }
}

void ImgVwWindow::UpdateLoaderStatsOverlayVisibilityForWindow()
{
    const auto visible = IsLoaderStatsOverlayKeyDown();
    if (!info_overlay_.SetStatsVisible(visible != FALSE))
    {
        return;
    }

    if (info_overlay_.stats_visible())
    {
        SetTimer(hwnd_, kLoaderStatsOverlayTimer, kLoaderStatsOverlayIntervalInMilliseconds, nullptr);
        UpdateLoadingProgressOverlayTimer();
    }
    else
    {
        KillTimer(hwnd_, kLoaderStatsOverlayTimer);
    }

    UpdateInfoOverlayForWindow();
}

void ImgVwWindow::UpdateInfoOverlayForWindow()
{
    if (IsInfoOverlayVisible())
    {
        RefreshLoaderStatsOverlay();
    }
    else
    {
        ClearInfoOverlay();
    }
}

void ImgVwWindow::ClearInfoOverlay()
{
    const auto previousrect = info_overlay_.Clear();
    if (!IsRectEmpty(&previousrect))
    {
        InvalidateRect(hwnd_, &previousrect, FALSE);
    }
}

std::wstring ImgVwWindow::BuildLoaderStatsOverlayText()
{
    const auto slideshowowner = owner_ == nullptr ? this : owner_;
    const auto slideshowrunning = slideshowowner->slideshow_.running();
    const auto randomslideshow = slideshowrunning && slideshowowner->slideshow_.random();
    const auto sharedstats = slideshowowner->browser_.GetStats();

    std::vector<ImgBrowserStats> targetstats;
    const auto addtargets = [&targetstats](ImgVwWindow* window) {
        if (window == nullptr)
        {
            return;
        }
        targetstats.push_back(window->browser_.GetStats());
    };

    addtargets(slideshowowner);
    for (const auto window_id : slideshowowner->multi_monitor_slideshow_.secondary_windows())
    {
        const auto window = ResolveSlideShowWindow(window_id);
        addtargets(window);
    }

    InfoOverlayStatsContext context;
    const auto temppath = ImgSettings::GetInstance().temppath();
    ULARGE_INTEGER freebytesavailable{};
    if (!temppath.empty() && GetDiskFreeSpaceEx(temppath.c_str(), &freebytesavailable, nullptr, nullptr))
    {
        context.has_free_bytes = true;
        context.free_bytes = freebytesavailable.QuadPart;
    }

    context.slideshow_running = slideshowrunning != FALSE;
    context.random_slideshow = randomslideshow != FALSE;
    if (slideshowrunning)
    {
        context.cycle = randomslideshow ? sharedstats.random : sharedstats.sequential;
        if (!randomslideshow && slideshowowner->multi_monitor_slideshow_.running() &&
            !slideshowowner->multi_monitor_slideshow_.sequential_cursor_path().empty())
        {
            context.cycle = slideshowowner->browser_.GetSequentialProgress(
                slideshowowner->multi_monitor_slideshow_.sequential_cursor_path());
        }
    }

    const auto currentitem = browser_.GetCurrentItem();
    const auto currentpath = browser_.GetCurrentFilePath();
    if (!currentpath.empty())
    {
        context.current_item_text = InfoOverlay::BuildItemText(currentitem.get(), currentpath);
    }
    return InfoOverlay::BuildStatsText(InfoOverlayStatsBuilder::Build(sharedstats, targetstats, context));
}

UINT ImgVwWindow::GetWindowDpi() const
{
    const auto user32 = GetModuleHandle(L"user32.dll");
    if (user32 != nullptr)
    {
        const auto get_dpi_for_window =
            reinterpret_cast<GetDpiForWindowProc>(GetProcAddress(user32, "GetDpiForWindow"));
        if (get_dpi_for_window != nullptr)
        {
            const auto dpi = get_dpi_for_window(hwnd_);
            if (dpi > 0)
            {
                return dpi;
            }
        }
    }

    WindowDeviceContext dc(hwnd_, GetDC(hwnd_));
    if (dc.valid())
    {
        const auto dpi = GetDeviceCaps(dc.get(), LOGPIXELSX);
        if (dpi > 0)
        {
            return static_cast<UINT>(dpi);
        }
    }

    return WindowGeometry::kDefaultDpi;
}

void ImgVwWindow::ResetLoaderStatsOverlayLayout()
{
    info_overlay_.ResetLayout();
}

void ImgVwWindow::RefreshLoaderStatsOverlay()
{
    if (!IsInfoOverlayVisible())
    {
        ClearInfoOverlay();
        return;
    }

    const auto currentitem = browser_.GetCurrentItem();
    const auto text = info_overlay_.stats_visible()
                          ? BuildLoaderStatsOverlayText()
                          : InfoOverlay::BuildItemText(currentitem.get(), browser_.GetCurrentFilePath());
    if (text.empty())
    {
        ClearInfoOverlay();
        return;
    }

    WindowDeviceContext dc(hwnd_, GetDC(hwnd_));
    if (!dc.valid())
    {
        return;
    }

    const auto rect = info_overlay_.CalculateRectangle(dc.get(), text, GetWindowDpi(), clientwidth_, clientheight_);

    RECT invalidaterect = rect;
    if (!IsRectEmpty(&info_overlay_.rectangle()))
    {
        UnionRect(&invalidaterect, &invalidaterect, &info_overlay_.rectangle());
    }

    if (!info_overlay_.SetContent(text, rect))
    {
        return;
    }

    InvalidateRect(hwnd_, &invalidaterect, FALSE);
}

void ImgVwWindow::DrawLoaderStatsOverlay(HDC dc, const ImgItem* item)
{
    const auto display_state = item == nullptr ? ImgItem::DisplayState{} : item->GetDisplayState();
    info_overlay_.Draw(dc, info_overlay_.rectangle(), info_overlay_.text(), display_state, GetWindowDpi(),
                       systemlighttheme_ != FALSE);
}

void ImgVwWindow::InvalidateScreen()
{
    slideshow_.SetWaitingForImage(false);
    if (IsFilenameOverlayVisible())
    {
        RefreshLoaderStatsOverlay();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (slideshow_.running())
    {
        RestartSlideShowTimer();
    }
}

void ImgVwWindow::ToggleFilenameOverlay()
{
    if (owner_ != nullptr)
    {
        owner_->ToggleFilenameOverlay();
        return;
    }

    info_overlay_visibility_.ToggleFilename(IsLoaderStatsOverlayKeyDown() != FALSE);
    UpdateInfoOverlayForWindow();
    for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
    {
        const auto window = ResolveSlideShowWindow(window_id);
        if (window != nullptr)
        {
            window->UpdateInfoOverlayForWindow();
        }
    }
}

void ImgVwWindow::UseBuiltInICCProfile()
{
    const auto result = ImgItem::ResetDefaultICCProfile();
    if (!result.Succeeded())
    {
        MessageBox(hwnd_, L"Could not remove the stored CMYK ICC profile.", L"CMYK profile error",
                   MB_OK | MB_ICONERROR);
        return;
    }

    browser_.ReloadCurrentItem();
    InvalidateScreen();
}

void ImgVwWindow::BrowseNext()
{
    if (browser_.MoveToNext())
    {
        InvalidateScreen();
    }
}

void ImgVwWindow::BrowsePrevious()
{
    if (browser_.MoveToPrevious())
    {
        InvalidateScreen();
    }
}

void ImgVwWindow::BrowseFirst()
{
    if (browser_.MoveToFirst())
    {
        InvalidateScreen();
    }
}

void ImgVwWindow::BrowseLast()
{
    if (browser_.MoveToLast())
    {
        InvalidateScreen();
    }
}

void ImgVwWindow::BrowseSubFolders()
{
    EnableBrowseSubFolders();
    for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
    {
        const auto window = ResolveSlideShowWindow(window_id);
        if (window != nullptr)
        {
            window->EnableBrowseSubFolders();
        }
    }
}

void ImgVwWindow::EnableBrowseSubFolders()
{
    browse_state_.EnableSubfolderSearch();
    browser_.BrowseSubFoldersAsync();
    InvalidateScreen();
}

void ImgVwWindow::HandleMouseWheel(WORD distance)
{
    if (owner_ != nullptr)
    {
        owner_->HandleMultiMonitorMouseWheel(this);
        return;
    }

    if (multi_monitor_slideshow_.running())
    {
        HandleMultiMonitorMouseWheel(this);
        return;
    }

    if (distance & 0x8000)
    {
        BrowseNext();
    }
    else
    {
        BrowsePrevious();
    }
}

void ImgVwWindow::HandleMultiMonitorMouseWheel(ImgVwWindow* target)
{
    if (multi_monitor_slideshow_.running())
    {
        AdvanceSharedSlide(target);
        RestartMultiMonitorSlideShowTimer();
    }
}

void ImgVwWindow::ToggleSlideShow(BOOL slideshowrandom)
{
    if (slideshow_.running() && !multi_monitor_slideshow_.running() &&
        slideshow_.random() == (slideshowrandom != FALSE))
    {
        StopSlideShow();
        return;
    }

    if (!HasImages())
    {
        return;
    }

    StopMultiMonitorSlideShow();
    StopSlideShow();
    slideshow_.SetMode(slideshowrandom ? SlideShowMode::Random : SlideShowMode::Sequential);
    if (slideshow_.random())
    {
        browser_.BeginRandomCycle();
    }
    StartSlideShow();
}

void ImgVwWindow::StartSlideShow()
{
    if (!slideshow_.running())
    {
        slideshow_.Start(slideshow_.random() ? SlideShowMode::Random : SlideShowMode::Sequential);
        DisplayCurrentSlideWhenReady();
    }
}

void ImgVwWindow::StopSlideShow()
{
    if (slideshow_.running())
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        slideshow_.Stop();
    }
}

void ImgVwWindow::ToggleMultiMonitorSlideShow(BOOL slideshowrandom)
{
    if (multi_monitor_slideshow_.running() && slideshow_.random() == (slideshowrandom != FALSE))
    {
        StopMultiMonitorSlideShow();
        return;
    }

    if (!HasImages())
    {
        return;
    }

    StartMultiMonitorSlideShow(slideshowrandom);
}

void ImgVwWindow::StartMultiMonitorSlideShow(BOOL slideshowrandom)
{
    if (!HasMultipleMonitors())
    {
        return;
    }

    StopSlideShow();
    DestroySlideShowWindows();

    slideshow_.SetMode(slideshowrandom ? SlideShowMode::Random : SlideShowMode::Sequential);
    if (slideshow_.random())
    {
        browser_.BeginRandomCycle();
    }
    slideshow_.Start(slideshow_.random() ? SlideShowMode::Random : SlideShowMode::Sequential);
    multi_monitor_slideshow_.Start();
    slideshow_.SetNeedsInitialAdvance(false);

    const auto primarymonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    monitor_placement_.SetCurrent(primarymonitor);
    RememberTargetLoadContext(clientwidth_, clientheight_, browser_.loadcontext());
    MonitorCreateContext context{hinst_, browse_state_.path(), primarymonitor, this, slideshow_.random()};
    EnumDisplayMonitors(nullptr, nullptr, CreateSlideShowWindowForMonitor, reinterpret_cast<LPARAM>(&context));
    if (!multi_monitor_slideshow_.secondary_windows().empty() && GetCapture() == hwnd_)
    {
        // Allow mouse input to reach slideshow windows on the other monitors.
        ApplyCursorCapture(false);
    }

    multi_monitor_slideshow_.SetPreloadedPathCount(PreloadMultiMonitorSlideShowContexts());

    for (std::size_t index = 0; index < MultiMonitorSlideShowWindowCount(); ++index)
    {
        if (slideshow_.random())
        {
            AdvanceSharedRandomSlide(MultiMonitorSlideShowWindowAt(index));
        }
        else if (index == 0)
        {
            DisplayCurrentSlideWithoutTimer();
            multi_monitor_slideshow_.SetSequentialCursorPath(browser_.GetCurrentFilePath());
        }
        else
        {
            AdvanceSharedSequentialSlide(MultiMonitorSlideShowWindowAt(index));
        }
    }

    RestartMultiMonitorSlideShowTimer();
}

void ImgVwWindow::StopMultiMonitorSlideShow()
{
    if (!multi_monitor_slideshow_.running() && multi_monitor_slideshow_.secondary_windows().empty())
    {
        return;
    }

    multi_monitor_slideshow_.Stop();
    StopSlideShow();
    DestroySlideShowWindows();
}

void ImgVwWindow::RestartMultiMonitorSlideShowTimer()
{
    if (multi_monitor_slideshow_.running() && slideshow_.running())
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        SetTimer(hwnd_, IDT_SLIDESHOW, slideshow_.interval(), nullptr);
    }
}

void ImgVwWindow::HandleMultiMonitorSlideShow()
{
    RefreshMultiMonitorPreloadContexts();
    const auto window = ResolveSlideShowWindow(multi_monitor_slideshow_.NextTarget(WindowId(hwnd_)));
    if (window != nullptr)
    {
        AdvanceSharedSlide(window);
    }

    RestartMultiMonitorSlideShowTimer();
}

void ImgVwWindow::RefreshMultiMonitorPreloadContexts()
{
    if (!multi_monitor_slideshow_.running())
    {
        return;
    }

    const auto foundimages = browser_.GetStats().found_images;
    if (foundimages > multi_monitor_slideshow_.preloaded_path_count())
    {
        multi_monitor_slideshow_.SetPreloadedPathCount(PreloadMultiMonitorSlideShowContexts());
    }
}

std::size_t ImgVwWindow::PreloadMultiMonitorSlideShowContexts()
{
    std::vector<std::shared_ptr<ImgBrowserLoadContext>> preloadedcontexts;
    std::size_t preloadedpathcount{};
    bool firstcontext = true;
    for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
    {
        const auto window = ResolveSlideShowWindow(window_id);
        if (window == nullptr)
        {
            continue;
        }

        window->UpdateInfoOverlayForWindow();
        const auto context = window->browser_.loadcontext();
        if (std::find(preloadedcontexts.begin(), preloadedcontexts.end(), context) != preloadedcontexts.end())
        {
            continue;
        }

        const auto contextpathcount = window->browser_.PreloadFrom(browser_);
        preloadedpathcount = firstcontext ? contextpathcount : (std::min)(preloadedpathcount, contextpathcount);
        firstcontext = false;
        preloadedcontexts.push_back(context);
    }

    return preloadedpathcount;
}

ImgVwWindow* ImgVwWindow::MultiMonitorSlideShowWindowAt(std::size_t index)
{
    return ResolveSlideShowWindow(multi_monitor_slideshow_.TargetAt(WindowId(hwnd_), index));
}

MultiMonitorWindowId ImgVwWindow::WindowId(HWND window)
{
    return reinterpret_cast<MultiMonitorWindowId>(window);
}

HWND ImgVwWindow::WindowHandle(MultiMonitorWindowId window)
{
    return reinterpret_cast<HWND>(window);
}

ImgVwWindow* ImgVwWindow::ResolveSlideShowWindow(MultiMonitorWindowId window)
{
    const auto handle = WindowHandle(window);
    if (handle == nullptr || !IsWindow(handle))
    {
        return nullptr;
    }

    const auto resolved = static_cast<ImgVwWindow*>(FromHandle(handle));
    return resolved != nullptr && resolved->hwnd() == handle ? resolved : nullptr;
}

std::size_t ImgVwWindow::MultiMonitorSlideShowWindowCount() const
{
    return multi_monitor_slideshow_.target_count();
}

std::shared_ptr<ImgBrowserLoadContext> ImgVwWindow::FindTargetLoadContext(INT width, INT height) const
{
    return multi_monitor_slideshow_.FindLoadContext(width, height);
}

void ImgVwWindow::RememberTargetLoadContext(INT width, INT height,
                                            const std::shared_ptr<ImgBrowserLoadContext>& context)
{
    multi_monitor_slideshow_.RememberLoadContext(width, height, context);
}

void ImgVwWindow::DestroySlideShowWindows()
{
    // Drop every registration before destruction. WM_NCDESTROY then performs an idempotent unregister while the owner
    // and its browser/load contexts are still alive.
    const auto windows = multi_monitor_slideshow_.ReleaseSecondaryWindows();
    for (const auto window_id : windows)
    {
        const auto window = WindowHandle(window_id);
        if (window != nullptr && IsWindow(window))
        {
            DestroyWindow(window);
        }
    }
}

void ImgVwWindow::OnSlideShowWindowDestroyed(ImgVwWindow* window)
{
    if (window != nullptr)
    {
        multi_monitor_slideshow_.UnregisterSecondaryWindow(WindowId(window->hwnd()));
    }
}

void ImgVwWindow::CloseOwnedWindows()
{
    DestroySlideShowWindows();
}

ImgVwWindow* ImgVwWindow::CommandTarget()
{
    return owner_ == nullptr ? this : owner_;
}

void ImgVwWindow::RestartSlideShowTimer()
{
    if (owner_ != nullptr)
    {
        return;
    }

    if (multi_monitor_slideshow_.running())
    {
        RestartMultiMonitorSlideShowTimer();
        return;
    }

    if (slideshow_.running())
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        if (!slideshow_.waiting_for_image())
        {
            SetTimer(hwnd_, IDT_SLIDESHOW, slideshow_.interval(), nullptr);
        }
    }
}

void ImgVwWindow::IncreaseSlideShowSpeed()
{
    if (slideshow_.IncreaseSpeed())
    {
        RestartSlideShowTimer();
        for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
        {
            const auto window = ResolveSlideShowWindow(window_id);
            if (window != nullptr)
            {
                window->slideshow_.SetInterval(slideshow_.interval());
            }
        }
    }
}

void ImgVwWindow::DecreaseSlideShowSpeed()
{
    if (slideshow_.DecreaseSpeed())
    {
        RestartSlideShowTimer();
        for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
        {
            const auto window = ResolveSlideShowWindow(window_id);
            if (window != nullptr)
            {
                window->slideshow_.SetInterval(slideshow_.interval());
            }
        }
    }
}

void ImgVwWindow::HandleSlideShow()
{
    if (multi_monitor_slideshow_.running())
    {
        HandleMultiMonitorSlideShow();
        return;
    }

    const auto navigation = slideshow_.OnTimer();
    if (navigation == SlideShowNavigation::Random)
    {
        AdvanceRandomSlide(TRUE);
        return;
    }

    if (navigation == SlideShowNavigation::Sequential && (browser_.MoveToNext() || browser_.MoveToFirst()))
    {
        DisplayCurrentSlideWhenReady();
    }
}

BOOL ImgVwWindow::AdvanceRandomSlide(BOOL restarttimer)
{
    if (slideshow_.waiting_for_image())
    {
        return FALSE;
    }

    if (!browser_.MoveToRandom())
    {
        return FALSE;
    }

    if (restarttimer)
    {
        DisplayCurrentSlideWhenReady();
    }
    else
    {
        DisplayCurrentSlideWithoutTimer();
    }

    slideshow_.SetNeedsInitialAdvance(false);
    return TRUE;
}

BOOL ImgVwWindow::AdvanceSharedSlide(ImgVwWindow* target)
{
    return slideshow_.random() ? AdvanceSharedRandomSlide(target) : AdvanceSharedSequentialSlide(target);
}

BOOL ImgVwWindow::AdvanceSharedSequentialSlide(ImgVwWindow* target)
{
    if (target == nullptr || target->slideshow_.waiting_for_image())
    {
        return FALSE;
    }

    if (!multi_monitor_slideshow_.sequential_cursor_path().empty())
    {
        browser_.MoveToItem(multi_monitor_slideshow_.sequential_cursor_path());
    }

    if (!browser_.MoveToNext() && !browser_.MoveToFirst())
    {
        return FALSE;
    }

    const auto filepath = browser_.GetCurrentFilePath();
    multi_monitor_slideshow_.SetSequentialCursorPath(filepath);

    if (target == this)
    {
        target->DisplayCurrentSlideWithoutTimer();
        target->slideshow_.SetNeedsInitialAdvance(false);
        return TRUE;
    }

    const auto displayed = target->DisplaySlidePath(filepath);
    RestoreSharedOwnerDisplayCursor();
    return displayed;
}

BOOL ImgVwWindow::AdvanceSharedRandomSlide(ImgVwWindow* target)
{
    if (target == nullptr || target->slideshow_.waiting_for_image())
    {
        return FALSE;
    }

    std::vector<std::wstring> visiblepaths;
    if (!display_session_.selected_path().empty())
    {
        visiblepaths.push_back(display_session_.selected_path());
    }
    for (const auto window_id : multi_monitor_slideshow_.secondary_windows())
    {
        const auto window = ResolveSlideShowWindow(window_id);
        if (window != nullptr && !window->display_session_.selected_path().empty())
        {
            visiblepaths.push_back(window->display_session_.selected_path());
        }
    }

    if (!browser_.MoveToRandomExcluding(visiblepaths))
    {
        RestoreSharedOwnerDisplayCursor();
        return FALSE;
    }

    const auto filepath = browser_.GetCurrentFilePath();
    if (target == this)
    {
        target->DisplayCurrentSlideWithoutTimer();
        target->slideshow_.SetNeedsInitialAdvance(false);
        return TRUE;
    }

    const auto displayed = target->DisplaySlidePath(filepath);
    RestoreSharedOwnerDisplayCursor();
    return displayed;
}

void ImgVwWindow::RestoreSharedOwnerDisplayCursor()
{
    if (!display_session_.selected_path().empty())
    {
        browser_.MoveToItem(display_session_.selected_path());
    }
}

BOOL ImgVwWindow::DisplaySlidePath(const std::wstring& filepath)
{
    if (filepath.empty() || !browser_.MoveToOrAddItem(filepath))
    {
        return FALSE;
    }

    DisplayCurrentSlideWithoutTimer();
    slideshow_.SetNeedsInitialAdvance(false);
    return TRUE;
}

void ImgVwWindow::DisplayCurrentSlideWhenReady()
{
    KillTimer(hwnd_, IDT_SLIDESHOW);
    DisplayCurrentSlideWithoutTimer();
    RestartSlideShowTimer();
}

void ImgVwWindow::DisplayCurrentSlideWithoutTimer()
{
    const auto imgitem = browser_.GetCurrentItem();
    if (imgitem == nullptr)
    {
        display_session_.Clear();
        slideshow_.OnDisplaySelection(false, false);
        return;
    }

    slideshow_.SetNeedsInitialAdvance(false);
    display_session_.Select(browser_.GetCurrentFilePath());
    const auto status = imgitem->status();
    slideshow_.OnDisplaySelection(true, status == ImgItem::Status::Ready || status == ImgItem::Status::Error);
    UpdateLoadingProgressOverlayTimer();
    if (!slideshow_.waiting_for_image())
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ImgVwWindow::HandleStartupExitConditions()
{
    if (owner_ != nullptr || !browse_state_.browser_initialized())
    {
        return;
    }

    if (!browser_.IsCollectingComplete())
    {
        // The collector posts its final notification immediately before its thread handle becomes signaled. Retry the
        // empty result after that narrow interval so completion cannot be lost.
        if (!browser_.HasFiles())
        {
            SetTimer(hwnd_, kBrowserCompletionRetryTimer, kBrowserCompletionRetryIntervalInMilliseconds, nullptr);
        }
        return;
    }

    KillTimer(hwnd_, kBrowserCompletionRetryTimer);

    const auto stats = browser_.GetStats();
    if (stats.found_images == 0)
    {
        ShowEmptyState(browse_state_.browse_subfolders()
                           ? L"No supported images were found in this folder or its subfolders."
                           : L"No supported images were found in this folder.",
                       browse_state_.browse_subfolders() ? FALSE : TRUE);
    }
    else if (IsEmptyStateVisible())
    {
        HideEmptyState();
    }
}

void ImgVwWindow::HandleBrowserChanged()
{
    if (browse_state_.browser_initialized() && IsEmptyStateVisible() && browser_.GetStats().found_images > 0)
    {
        HideEmptyState();
    }
    HandleStartupExitConditions();
    if (owner_ == nullptr && browser_.IsCollectingComplete())
    {
        RefreshMultiMonitorPreloadContexts();
    }
    UpdateLoadingProgressOverlayTimer();

    if (slideshow_.waiting_for_image())
    {
        const auto imgitem = browser_.GetCurrentItem();
        if (imgitem == nullptr)
        {
            return;
        }

        const auto status = imgitem->status();
        if (status != ImgItem::Status::Ready && status != ImgItem::Status::Error)
        {
            return;
        }

        slideshow_.OnImageReady();
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (owner_ == nullptr && slideshow_.running() && !multi_monitor_slideshow_.running())
        {
            RestartSlideShowTimer();
        }

        return;
    }

    if (slideshow_.needs_initial_advance() && slideshow_.running() && slideshow_.random())
    {
        if (owner_ != nullptr)
        {
            owner_->AdvanceSharedSlide(this);
        }
        else if (multi_monitor_slideshow_.running())
        {
            AdvanceSharedSlide(this);
        }
        else
        {
            AdvanceRandomSlide(TRUE);
        }

        return;
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

BOOL ImgVwWindow::HandleMouseMove(WPARAM wParam, LPARAM lParam)
{
    if (IsEmptyStateVisible())
    {
        return FALSE;
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const auto actions = cursor_controller_.OnMouseMove(MAKEPOINTS(lParam), counter.QuadPart);
    ApplyCursorVisibility(actions.visibility);
    if (actions.arm_idle_timer)
    {
        SetTimer(hwnd_, IDT_HIDEMOUSE, kMouseHideIntervalInMilliseconds, nullptr);
    }

    return FALSE;
}

void ImgVwWindow::HandleHideMouseCursor()
{
    if (IsEmptyStateVisible())
    {
        KillTimer(hwnd_, IDT_HIDEMOUSE);
        cursor_controller_.CancelIdleTimer();
        return;
    }

    KillTimer(hwnd_, IDT_HIDEMOUSE);
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const auto actions =
        cursor_controller_.OnIdleTimer(counter.QuadPart, qpcfrequency_.QuadPart, kMouseHideIntervalInMilliseconds);
    ApplyCursorVisibility(actions.visibility);
    if (actions.arm_idle_timer)
    {
        SetTimer(hwnd_, IDT_HIDEMOUSE, actions.idle_timer_delay, nullptr);
    }
}

void ImgVwWindow::ApplyCursorVisibility(CursorVisibilityAction action)
{
    if (action == CursorVisibilityAction::Show)
    {
        ShowCursor(TRUE);
    }
    else if (action == CursorVisibilityAction::Hide)
    {
        ShowCursor(FALSE);
    }
}

void ImgVwWindow::ApplyCursorCapture(bool captured)
{
    if (!cursor_controller_.SetCaptured(captured))
    {
        return;
    }
    if (captured)
    {
        SetCapture(hwnd_);
    }
    else if (GetCapture() == hwnd_)
    {
        ReleaseCapture();
    }
}

void ImgVwWindow::DeleteCurrentItem(BOOL allowundo)
{
    if (browser_.GetCurrentItem() == nullptr)
    {
        return;
    }

    const auto filepath = browser_.GetCurrentFilePath();
    const auto mode = allowundo ? FileDeleteMode::Recycle : FileDeleteMode::Permanent;
    const auto result = file_operations_.Delete(hwnd_, filepath, mode);
    if (result.status == FileOperationStatus::Succeeded)
    {
        browser_.RemoveCurrentItem();
        if (browser_.GetCurrentItem() == nullptr)
        {
            if (!browser_.MoveToPrevious())
            {
                CloseWindow();
            }
        }

        InvalidateScreen();
    }
}

BOOL ImgVwWindow::SelectDefaultICCProfile()
{
    OPENFILENAME ofn;
    TCHAR szFile[MAX_PATH]{};
    BOOL profile_selected = FALSE;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = _countof(szFile);
    ofn.lpstrFilter = L"ICC Profile\0*.icc\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Select default CMYK ICC profile...";
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    ApplyCursorVisibility(cursor_controller_.SetVisible(true));
    if (GetOpenFileName(&ofn))
    {
        const auto validation = ImgItem::ValidateCMYKICCProfile(ofn.lpstrFile);
        if (!validation.Succeeded())
        {
            MessageBox(hwnd_, L"The selected file is not a valid CMYK ICC profile.", L"Invalid CMYK ICC profile",
                       MB_OK | MB_ICONERROR);
        }
        else
        {
            TCHAR appdatapath[MAX_PATH]{};
            TCHAR imgvwappdatapath[MAX_PATH]{};
            TCHAR iccpath[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdatapath)) &&
                PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath) != nullptr)
            {
                const auto result = SHCreateDirectoryEx(hwnd_, imgvwappdatapath, nullptr);
                if ((result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS) &&
                    PathCombine(iccpath, imgvwappdatapath, ImgItem::kDefaultICCProfileFilename) != nullptr)
                {
                    profile_selected = CopyFile(ofn.lpstrFile, iccpath, FALSE);
                }
            }

            if (!profile_selected)
            {
                MessageBox(hwnd_, L"Could not store the selected CMYK ICC profile.", L"CMYK profile error",
                           MB_OK | MB_ICONERROR);
            }
        }
    }

    ApplyCursorVisibility(cursor_controller_.SetVisible(false));
    return profile_selected;
}

void ImgVwWindow::UpdateContextMenuForMonitorCount(HMENU menu) const
{
    if (menu == nullptr || HasMultipleMonitors())
    {
        return;
    }

    DeleteMenu(menu, IDR_TOGGLESS_MULTI, MF_BYCOMMAND);
    DeleteMenu(menu, IDR_TOGGLESS_MULTI_RANDOM, MF_BYCOMMAND);
}

void ImgVwWindow::UpdateContextMenuForImageAvailability(HMENU menu)
{
    const auto target = CommandTarget();
    if (menu == nullptr || (target != nullptr && target->HasImages()))
    {
        return;
    }

    for (const auto command : kImageCommandIds)
    {
        EnableMenuItem(menu, command, MF_BYCOMMAND | MF_GRAYED);
    }
}

void ImgVwWindow::HandleContextMenu(LPARAM lParam)
{
    RECT rc;
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    GetClientRect(hwnd_, &rc);
    ScreenToClient(hwnd_, &pt);

    if (PtInRect(&rc, pt))
    {
        ClientToScreen(hwnd_, &pt);

        const auto root = LoadMenu(hinst_, L"IMGPOPUP");
        const auto popup = GetSubMenu(root, 0);
        UpdateContextMenuForMonitorCount(popup);
        UpdateContextMenuForImageAvailability(popup);

        const auto restore_viewer_cursor = !IsEmptyStateVisible();
        if (restore_viewer_cursor)
        {
            ApplyCursorVisibility(cursor_controller_.SetVisible(true));
        }
        TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        if (restore_viewer_cursor)
        {
            ApplyCursorVisibility(cursor_controller_.SetVisible(false));
        }

        DestroyMenu(root);
    }
}

BOOL CALLBACK ImgVwWindow::AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        PostMessage(hwndDlg, WM_NEXTDLGCTL, 0, FALSE);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hwndDlg, TRUE);
            return TRUE;
        }

        break;
    default:
        break;
    }

    return FALSE;
}

void ImgVwWindow::CloseWindow()
{
    PostMessage(hwnd_, WM_CLOSE, 0, 0);
}

void ImgVwWindow::OnNCDestroy()
{
    CloseOwnedWindows();
    StopSlideShow();
    KillTimer(hwnd_, kLoaderStatsOverlayTimer);
    KillTimer(hwnd_, kLoadingProgressOverlayTimer);
    KillTimer(hwnd_, kBrowserCompletionRetryTimer);
    browser_.StopBrowsing();
    backgroundbrush_.reset();
    if (owner_ != nullptr)
    {
        owner_->OnSlideShowWindowDestroyed(this);
    }
}

LRESULT ImgVwWindow::HandleCommand(UINT command, UINT notification, LPARAM lparam)
{
    if (owner_ != nullptr)
    {
        return SendMessage(CommandTarget()->hwnd(), WM_COMMAND, MAKEWPARAM(command, notification), lparam);
    }

    if (IsImageCommand(command) && !HasImages() && !(command == IDR_TOGGLE_FILENAME && notification == 1))
    {
        return FALSE;
    }

    switch (command)
    {
    case IDM_OPEN_IMAGE:
        OpenImage();
        break;
    case IDM_OPEN_FOLDER:
        OpenFolder();
        break;
    case IDM_SEARCH_SUBFOLDERS:
        BrowseEmptyStateSubFolders();
        break;
    case IDM_ABOUT:
        ApplyCursorVisibility(cursor_controller_.SetVisible(true));
        DialogBox(hinst_, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd_, reinterpret_cast<DLGPROC>(AboutDialogProc));
        ApplyCursorVisibility(cursor_controller_.SetVisible(IsEmptyStateVisible() != FALSE));
        break;
    case IDR_ENTER:
        ActivateEmptyStateButton();
        break;
    case IDR_TOGGLE_FILENAME:
        if (IsEmptyStateVisible())
        {
            ActivateEmptyStateButton();
        }
        else
        {
            ToggleFilenameOverlay();
        }
        break;
    case IDR_NEXT:
        BrowseNext();
        break;
    case IDR_PREVIOUS:
        BrowsePrevious();
        break;
    case IDR_FIRST:
        BrowseFirst();
        break;
    case IDR_LAST:
        BrowseLast();
        break;
    case IDR_RECYCLE:
    case IDR_DELETE:
        DeleteCurrentItem(command == IDR_RECYCLE);
        break;
    case IDR_TOGGLESS:
        ToggleSlideShow(FALSE);
        break;
    case IDR_TOGGLESSR:
        ToggleSlideShow(TRUE);
        break;
    case IDR_TOGGLESS_MULTI:
        ToggleMultiMonitorSlideShow(FALSE);
        break;
    case IDR_TOGGLESS_MULTI_RANDOM:
        ToggleMultiMonitorSlideShow(TRUE);
        break;
    case IDR_INCSSS:
        IncreaseSlideShowSpeed();
        break;
    case IDR_DECSSS:
        DecreaseSlideShowSpeed();
        break;
    case IDR_RECURSE:
        BrowseSubFolders();
        break;
    case IDM_LOADICC:
        if (SelectDefaultICCProfile())
        {
            ImgItem::UnloadDefaultICCProfile();
            browser_.ReloadCurrentItem();
            InvalidateScreen();
        }
        break;
    case IDM_USEBUILTINICC:
        UseBuiltInICCProfile();
        break;
    case IDM_EXIT:
    case IDR_ESCAPE:
        CloseWindow();
        break;
    default:
        break;
    }

    return FALSE;
}

LRESULT ImgVwWindow::HandleTimer(UINT_PTR timer, LPARAM callback)
{
    switch (timer)
    {
    case IDT_SLIDESHOW:
        HandleSlideShow();
        return 0;
    case IDT_HIDEMOUSE:
        HandleHideMouseCursor();
        return 0;
    case kLoaderStatsOverlayTimer:
        UpdateLoaderStatsOverlayVisibility();
        if (info_overlay_.stats_visible())
        {
            RefreshLoaderStatsOverlay();
        }
        return 0;
    case kLoadingProgressOverlayTimer:
        UpdateLoadingProgressOverlayTimer();
        return 0;
    case kBrowserCompletionRetryTimer:
        KillTimer(hwnd_, kBrowserCompletionRetryTimer);
        HandleStartupExitConditions();
        return 0;
    default:
        return Window::HandleMessage(WM_TIMER, timer, callback);
    }
}

LRESULT ImgVwWindow::HandleMouseMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
        if (BeginWindowDrag(lParam))
        {
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        FinishWindowDrag();
        return 0;
    case WM_MOUSEMOVE:
        if (UpdateWindowDrag(wParam, lParam) || HandleMouseMove(wParam, lParam))
        {
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return FALSE;
    default:
        break;
    }

    return Window::HandleMessage(message, wParam, lParam);
}

LRESULT ImgVwWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case kBrowserChangedMessage:
        if (!browser_.IsCurrentNotification(wParam, lParam))
        {
            return 0;
        }
        HandleBrowserChanged();
        return 0;
    case WM_ACTIVATE:
        return FALSE;
    case WM_CREATE:
        return OnCreate();
    case WM_ERASEBKGND:
        return TRUE;
    case WM_SIZE:
        HandleSize(wParam, lParam);
        return FALSE;
    case WM_DPICHANGED:
        HandleDpiChanged(lParam);
        return 0;
    case WM_WINDOWPOSCHANGED:
        HandleWindowPosChanged();
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        UpdateSystemTheme();
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        UpdateLoaderStatsOverlayVisibility();
        break;
    case WM_SETFOCUS:
        return FALSE;
    case WM_COMMAND:
        return HandleCommand(LOWORD(wParam), HIWORD(wParam), lParam);
    case WM_DRAWITEM:
        if (wParam == IDM_OPEN_IMAGE || wParam == IDM_OPEN_FOLDER || wParam == IDM_SEARCH_SUBFOLDERS ||
            wParam == IDM_EXIT)
        {
            empty_state_view_.DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), GetWindowDpi(),
                                         systemlighttheme_ != FALSE);
            return TRUE;
        }
        break;
    case WM_CONTEXTMENU:
        HandleContextMenu(lParam);
        return TRUE;
    case WM_DROPFILES:
        HandleDroppedFiles(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
        return HandleMouseMessage(uMsg, wParam, lParam);
    case WM_SYSCOMMAND:
        if (LOWORD(wParam) == SC_CLOSE)
        {
            CloseWindow();
        }

        return FALSE;
    case WM_TIMER:
        return HandleTimer(static_cast<UINT_PTR>(wParam), lParam);
    case WM_NCDESTROY:
        OnNCDestroy();
        break;
    case WM_DESTROY:
        if (primarywindow_)
        {
            PostQuitMessage(0);
        }

        return 0;
    default:
        break;
    }

    return Window::HandleMessage(uMsg, wParam, lParam);
}

#include "ImgVwWindow.h"

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
    HKEY personalize_key{};
    const auto open_result =
        RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                     KEY_QUERY_VALUE, &personalize_key);
    if (open_result != ERROR_SUCCESS)
    {
        // Windows XP has no AppsUseLightTheme preference. Keep its traditional light appearance.
        return TRUE;
    }

    DWORD value{};
    DWORD value_size = sizeof(value);
    const auto query_result = RegQueryValueEx(personalize_key, L"AppsUseLightTheme", nullptr, nullptr,
                                              reinterpret_cast<LPBYTE>(&value), &value_size);
    RegCloseKey(personalize_key);
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
        self->backgroundbrush_ = CreateSolidBrush(RGB(0, 0, 0));
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

        self->backgroundbrush_ = CreateSolidBrush(RGB(0, 0, 0));
        self->manualcursor_ = TRUE;
        self->dontfillbackground_ = TRUE;
        const auto ownerhwnd = owner == nullptr ? nullptr : owner->hwnd();
        if (self->WinCreateWindow(WS_EX_TOOLWINDOW, L"ImgVw", WS_POPUP, monitorinfo.rcMonitor.left,
                                  monitorinfo.rcMonitor.top, width, height, ownerhwnd, nullptr))
        {
            self->currentmonitor_ = monitor;
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
        context->owner->slideshowwindows_.push_back(window);
        window->slideshowrandom_ = context->slideshowrandom;
        window->slideshowrunning_ = TRUE;
        ShowWindow(window->hwnd(), SW_SHOWNOACTIVATE);
        if (context->owner->browsesubfolders_)
        {
            window->EnableBrowseSubFolders();
        }
    }

    return TRUE;
}

LRESULT ImgVwWindow::OnCreate()
{
    QueryPerformanceFrequency(&qpcfrequency_);
    arrowcursor_ = LoadCursor(nullptr, IDC_ARROW);
    SetCursor(arrowcursor_);
    if (primarywindow_)
    {
        SetCapture(hwnd_);
        ShowCursor(FALSE);
    }

    InitializeMonitorState();
    UpdateSystemTheme();
    if (owner_ == nullptr)
    {
        empty_state_view_.Initialize(hwnd_, hinst_);
        DragAcceptFiles(hwnd_, TRUE);
    }
    if (path_.empty())
    {
        ShowEmptyState(L"Open an image or browse a folder to begin.", FALSE);
    }
    else if (!InitializeBrowser(path_))
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
                                                          backgroundbrush_,
                                                          client_rectangle,
                                                          slideshowwaitingforimage_,
                                                          firstimagepaint_,
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
            firstimagepaint_ = FALSE;
        }

        if (DisplayPresenter::ShouldDrawOnlyOverlay(IsInfoOverlayVisible() != FALSE, snapshot.state.status,
                                                    info_overlay_.rectangle(), pps->rcPaint))
        {
            DrawLoaderStatsOverlay(pps->hdc, imgitem.get());
            return;
        }

        if (presentation.IsImageReady())
        {
            paintedslidepath_ = snapshot.path;
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
    browserinitialized_ = TRUE;
    browseuistate_ = BrowseUiState::Collecting;
    return TRUE;
}

BOOL ImgVwWindow::OpenPath(const std::wstring& path)
{
    if (path.empty())
    {
        return FALSE;
    }

    const auto was_empty = IsEmptyStateVisible();
    if (was_empty)
    {
        HideEmptyState();
    }
    StopMultiMonitorSlideShow();
    StopSlideShow();
    if (!InitializeBrowser(path, TRUE))
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

    targetloadcontexts_.clear();
    path_ = path;
    browsesubfolders_ = FALSE;
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
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        ShowCursor(TRUE);
    }

    SelectPath(path_picker_.SelectImage(hwnd_));
    empty_state_view_.RestoreFocus(IDM_OPEN_IMAGE);

    if (restore_viewer_input && !IsEmptyStateVisible())
    {
        SetCapture(hwnd_);
        ShowCursor(FALSE);
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
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        ShowCursor(TRUE);
    }

    SelectPath(path_picker_.SelectFolder(hwnd_));
    empty_state_view_.RestoreFocus(IDM_OPEN_FOLDER);

    if (restore_viewer_input && !IsEmptyStateVisible())
    {
        SetCapture(hwnd_);
        ShowCursor(FALSE);
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
    if (!browserinitialized_ || browsesubfolders_)
    {
        return;
    }

    if (browser_.BrowseSubFoldersAsync())
    {
        browsesubfolders_ = TRUE;
        for (const auto window : slideshowwindows_)
        {
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
    return browserinitialized_ && browser_.HasFiles();
}

void ImgVwWindow::ShowEmptyState(const std::wstring& message, BOOL show_search_subfolders)
{
    if (owner_ != nullptr)
    {
        return;
    }

    const auto was_visible = IsEmptyStateVisible();
    empty_state_view_.Show(message, browserinitialized_ != FALSE, show_search_subfolders != FALSE);
    if (!was_visible)
    {
        KillTimer(hwnd_, IDT_HIDEMOUSE);
        mousehidetimerstarted_ = FALSE;
        if (GetCapture() == hwnd_)
        {
            ReleaseCapture();
        }
        ShowCursor(TRUE);
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
        SetCapture(hwnd_);
        ShowCursor(FALSE);
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
    const auto restorepath = paintedslidepath_.empty() ? displayslidepath_ : paintedslidepath_;
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
    if (suggestedrect != nullptr)
    {
        SetWindowPos(hwnd_, nullptr, suggestedrect->left, suggestedrect->top,
                     suggestedrect->right - suggestedrect->left, suggestedrect->bottom - suggestedrect->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
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
    currentmonitor_ = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
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
    if (monitor == nullptr || monitor == currentmonitor_)
    {
        return;
    }

    ResetLoaderStatsOverlayLayout();
    currentmonitor_ = monitor;
    if (!draggingwindow_)
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

    const auto width = monitorinfo.rcMonitor.right - monitorinfo.rcMonitor.left;
    const auto height = monitorinfo.rcMonitor.bottom - monitorinfo.rcMonitor.top;
    if (width <= 0 || height <= 0)
    {
        return FALSE;
    }

    return SetWindowPos(hwnd_, nullptr, monitorinfo.rcMonitor.left, monitorinfo.rcMonitor.top, width, height,
                        SWP_NOACTIVATE | SWP_NOZORDER);
}

BOOL ImgVwWindow::HasMultipleMonitors() const
{
    return GetSystemMetrics(SM_CMONITORS) > 1;
}

BOOL ImgVwWindow::BeginWindowDrag(LPARAM lParam)
{
    if (multimonitorslideshowrunning_ || owner_ != nullptr || !HasMultipleMonitors())
    {
        return FALSE;
    }

    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (!ClientToScreen(hwnd_, &point))
    {
        return FALSE;
    }

    if (!GetWindowRect(hwnd_, &dragstartwindowrect_))
    {
        return FALSE;
    }

    dragstartpoint_ = point;
    draggingwindow_ = TRUE;
    ShowCursor(TRUE);
    return TRUE;
}

BOOL ImgVwWindow::UpdateWindowDrag(WPARAM wParam, LPARAM lParam)
{
    if (!draggingwindow_)
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

    const auto x = dragstartwindowrect_.left + (point.x - dragstartpoint_.x);
    const auto y = dragstartwindowrect_.top + (point.y - dragstartpoint_.y);
    return SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void ImgVwWindow::EndWindowDrag()
{
    if (!draggingwindow_)
    {
        return;
    }

    draggingwindow_ = FALSE;
    ShowCursor(FALSE);
}

void ImgVwWindow::FinishWindowDrag()
{
    if (!draggingwindow_)
    {
        return;
    }

    const auto monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    EndWindowDrag();
    if (monitor != nullptr)
    {
        currentmonitor_ = monitor;
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
    const auto memorydc = CreateCompatibleDC(dc);
    const auto bitmap = memorydc == nullptr ? nullptr : CreateCompatibleBitmap(dc, paintwidth, paintheight);
    if (memorydc == nullptr || bitmap == nullptr)
    {
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }
        if (memorydc != nullptr)
        {
            DeleteDC(memorydc);
        }
        FillRect(dc, &paintrect, backgroundbrush_);
        info_overlay_.Draw(dc, overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
        return overlaydrawn;
    }

    const auto previousbitmap = SelectObject(memorydc, bitmap);
    if (previousbitmap == nullptr || previousbitmap == HGDI_ERROR)
    {
        DeleteObject(bitmap);
        DeleteDC(memorydc);
        FillRect(dc, &paintrect, backgroundbrush_);
        info_overlay_.Draw(dc, overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
        return overlaydrawn;
    }

    POINT previousorigin{};
    SetViewportOrgEx(memorydc, -paintrect.left, -paintrect.top, &previousorigin);
    FillRect(memorydc, &paintrect, backgroundbrush_);
    info_overlay_.Draw(memorydc, overlayrect, text, {}, GetWindowDpi(), systemlighttheme_ != FALSE);
    SetViewportOrgEx(memorydc, previousorigin.x, previousorigin.y, nullptr);

    BitBlt(dc, paintrect.left, paintrect.top, paintwidth, paintheight, memorydc, 0, 0, SRCCOPY);
    SelectObject(memorydc, previousbitmap);
    DeleteObject(bitmap);
    DeleteDC(memorydc);
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
    FillRect(dc, &paintrect, backgroundbrush_);
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
    return stateowner->filenameoverlayenabled_ && !IsLoaderStatsOverlayKeyDown();
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

    if (IsLoaderStatsOverlayKeyDown())
    {
        filenameoverlayenabled_ = FALSE;
    }

    UpdateLoaderStatsOverlayVisibilityForWindow();
    for (const auto window : slideshowwindows_)
    {
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
    const auto slideshowrunning = slideshowowner->slideshowrunning_;
    const auto randomslideshow = slideshowrunning && slideshowowner->slideshowrandom_;
    const auto sharedstats = slideshowowner->browser_.GetStats();

    struct TargetSizeStats
    {
        ImgCacheSizeStats size;
        ImgLoaderStats loader;
    };

    std::vector<TargetSizeStats> targetstats;
    const auto addtargets = [&targetstats](ImgVwWindow* window) {
        if (window == nullptr)
        {
            return;
        }

        const auto stats = window->browser_.GetStats();
        const auto duplicate =
            std::find_if(targetstats.begin(), targetstats.end(), [&stats](const TargetSizeStats& target) {
                return target.size.targetwidth == stats.targetwidth && target.size.targetheight == stats.targetheight;
            });
        if (duplicate != targetstats.end())
        {
            return;
        }

        TargetSizeStats target{{}, stats.loader};
        target.size.targetwidth = stats.targetwidth;
        target.size.targetheight = stats.targetheight;
        const auto size = std::find_if(stats.sizes.begin(), stats.sizes.end(), [&stats](const ImgCacheSizeStats& item) {
            return item.targetwidth == stats.targetwidth && item.targetheight == stats.targetheight;
        });
        if (size != stats.sizes.end())
        {
            target.size = *size;
        }
        targetstats.push_back(target);
    };

    addtargets(slideshowowner);
    for (const auto window : slideshowowner->slideshowwindows_)
    {
        addtargets(window);
    }

    InfoOverlayStatsSnapshot snapshot;
    snapshot.found_images = sharedstats.found_images;
    for (const auto& target : targetstats)
    {
        const auto& size_stats = target.size;
        snapshot.targets.push_back({size_stats.targetwidth, size_stats.targetheight, size_stats.queued,
                                    size_stats.loading, size_stats.ready, size_stats.error, size_stats.temp_file_bytes,
                                    target.loader.queued, target.loader.free_slots, target.loader.maximum_slots});
    }

    const auto temppath = ImgSettings::GetInstance().temppath();
    ULARGE_INTEGER freebytesavailable{};
    if (!temppath.empty() && GetDiskFreeSpaceEx(temppath.c_str(), &freebytesavailable, nullptr, nullptr))
    {
        snapshot.has_free_bytes = true;
        snapshot.free_bytes = freebytesavailable.QuadPart;
    }

    snapshot.slideshow_running = slideshowrunning != FALSE;
    snapshot.random_slideshow = randomslideshow != FALSE;
    if (slideshowrunning)
    {
        auto progress = randomslideshow ? sharedstats.random : sharedstats.sequential;
        if (!randomslideshow && slideshowowner->multimonitorslideshowrunning_ &&
            !slideshowowner->multimonitorslideshowcursorpath_.empty())
        {
            progress = slideshowowner->browser_.GetSequentialProgress(slideshowowner->multimonitorslideshowcursorpath_);
        }
        snapshot.cycle_position = progress.position;
        snapshot.cycle_total = progress.total;
    }

    const auto currentitem = browser_.GetCurrentItem();
    const auto currentpath = browser_.GetCurrentFilePath();
    if (!currentpath.empty())
    {
        snapshot.current_item_text = InfoOverlay::BuildItemText(currentitem.get(), currentpath);
    }
    return InfoOverlay::BuildStatsText(snapshot);
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

    HDC dc = GetDC(hwnd_);
    if (dc != nullptr)
    {
        const auto dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd_, dc);
        if (dpi > 0)
        {
            return static_cast<UINT>(dpi);
        }
    }

    return WindowGeometry::kDefaultDpi;
}

INT ImgVwWindow::ScaleForWindowDpi(INT value) const
{
    return WindowGeometry::ScaleForDpi(value, GetWindowDpi());
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

    HDC dc = GetDC(hwnd_);
    if (dc == nullptr)
    {
        return;
    }

    const auto rect = info_overlay_.CalculateRectangle(dc, text, GetWindowDpi(), clientwidth_, clientheight_);
    ReleaseDC(hwnd_, dc);

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
    slideshowwaitingforimage_ = FALSE;
    if (IsFilenameOverlayVisible())
    {
        RefreshLoaderStatsOverlay();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (slideshowrunning_)
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

    filenameoverlayenabled_ = IsLoaderStatsOverlayKeyDown() ? FALSE : !filenameoverlayenabled_;
    UpdateInfoOverlayForWindow();
    for (const auto window : slideshowwindows_)
    {
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
    for (const auto window : slideshowwindows_)
    {
        window->EnableBrowseSubFolders();
    }
}

void ImgVwWindow::EnableBrowseSubFolders()
{
    browsesubfolders_ = TRUE;
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

    if (multimonitorslideshowrunning_)
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
    if (multimonitorslideshowrunning_)
    {
        AdvanceSharedSlide(target);
        RestartMultiMonitorSlideShowTimer();
    }
}

void ImgVwWindow::ToggleSlideShow(BOOL slideshowrandom)
{
    if (slideshowrunning_ && !multimonitorslideshowrunning_ && slideshowrandom_ == slideshowrandom)
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
    slideshowrandom_ = slideshowrandom;
    if (slideshowrandom_)
    {
        browser_.BeginRandomCycle();
    }
    StartSlideShow();
}

void ImgVwWindow::StartSlideShow()
{
    if (!slideshowrunning_)
    {
        slideshowrunning_ = TRUE;
        DisplayCurrentSlideWhenReady();
    }
}

void ImgVwWindow::StopSlideShow()
{
    if (slideshowrunning_)
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        slideshowrunning_ = FALSE;
        slideshowwaitingforimage_ = FALSE;
        slideshowneedsinitialadvance_ = FALSE;
    }
}

void ImgVwWindow::ToggleMultiMonitorSlideShow(BOOL slideshowrandom)
{
    if (multimonitorslideshowrunning_ && slideshowrandom_ == slideshowrandom)
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

    slideshowrandom_ = slideshowrandom;
    if (slideshowrandom_)
    {
        browser_.BeginRandomCycle();
    }
    slideshowrunning_ = TRUE;
    multimonitorslideshowrunning_ = TRUE;
    multimonitorslideshowindex_ = 0;
    multimonitorslideshowcursorpath_.clear();
    slideshowneedsinitialadvance_ = FALSE;

    const auto primarymonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    currentmonitor_ = primarymonitor;
    RememberTargetLoadContext(clientwidth_, clientheight_, browser_.loadcontext());
    MonitorCreateContext context{hinst_, path_, primarymonitor, this, slideshowrandom_};
    EnumDisplayMonitors(nullptr, nullptr, CreateSlideShowWindowForMonitor, reinterpret_cast<LPARAM>(&context));
    if (!slideshowwindows_.empty() && GetCapture() == hwnd_)
    {
        // Allow mouse input to reach slideshow windows on the other monitors.
        ReleaseCapture();
    }

    multimonitorslideshowpreloadcount_ = PreloadMultiMonitorSlideShowContexts();

    for (std::size_t index = 0; index < MultiMonitorSlideShowWindowCount(); ++index)
    {
        if (slideshowrandom_)
        {
            AdvanceSharedRandomSlide(MultiMonitorSlideShowWindowAt(index));
        }
        else if (index == 0)
        {
            DisplayCurrentSlideWithoutTimer();
            multimonitorslideshowcursorpath_ = browser_.GetCurrentFilePath();
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
    if (!multimonitorslideshowrunning_ && slideshowwindows_.empty())
    {
        return;
    }

    multimonitorslideshowrunning_ = FALSE;
    multimonitorslideshowindex_ = 0;
    multimonitorslideshowpreloadcount_ = 0;
    multimonitorslideshowcursorpath_.clear();
    StopSlideShow();
    DestroySlideShowWindows();
}

void ImgVwWindow::RestartMultiMonitorSlideShowTimer()
{
    if (multimonitorslideshowrunning_ && slideshowrunning_)
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        SetTimer(hwnd_, IDT_SLIDESHOW, slideshowinterval_, nullptr);
    }
}

void ImgVwWindow::HandleMultiMonitorSlideShow()
{
    const auto count = MultiMonitorSlideShowWindowCount();
    if (count == 0)
    {
        return;
    }

    auto window = MultiMonitorSlideShowWindowAt(multimonitorslideshowindex_ % count);
    multimonitorslideshowindex_ = (multimonitorslideshowindex_ + 1) % count;
    if (window != nullptr)
    {
        AdvanceSharedSlide(window);
    }

    RestartMultiMonitorSlideShowTimer();
}

std::size_t ImgVwWindow::PreloadMultiMonitorSlideShowContexts()
{
    std::vector<std::shared_ptr<ImgBrowserLoadContext>> preloadedcontexts;
    std::size_t preloadedpathcount{};
    bool firstcontext = true;
    for (const auto window : slideshowwindows_)
    {
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
    if (index == 0)
    {
        return this;
    }

    const auto childindex = index - 1;
    return childindex < slideshowwindows_.size() ? slideshowwindows_[childindex] : nullptr;
}

std::size_t ImgVwWindow::MultiMonitorSlideShowWindowCount() const
{
    return 1 + slideshowwindows_.size();
}

std::shared_ptr<ImgBrowserLoadContext> ImgVwWindow::FindTargetLoadContext(INT width, INT height) const
{
    const auto match = std::find_if(targetloadcontexts_.begin(), targetloadcontexts_.end(),
                                    [width, height](const TargetLoadContext& context) {
                                        return context.width == width && context.height == height;
                                    });
    return match == targetloadcontexts_.end() ? std::shared_ptr<ImgBrowserLoadContext>() : match->context;
}

void ImgVwWindow::RememberTargetLoadContext(INT width, INT height,
                                            const std::shared_ptr<ImgBrowserLoadContext>& context)
{
    if (context == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    targetloadcontexts_.erase(std::remove_if(targetloadcontexts_.begin(), targetloadcontexts_.end(),
                                             [width, height, &context](const TargetLoadContext& item) {
                                                 return item.context == context &&
                                                        (item.width != width || item.height != height);
                                             }),
                              targetloadcontexts_.end());

    const auto match = std::find_if(
        targetloadcontexts_.begin(), targetloadcontexts_.end(),
        [width, height](const TargetLoadContext& item) { return item.width == width && item.height == height; });
    if (match == targetloadcontexts_.end())
    {
        targetloadcontexts_.push_back({width, height, context});
    }
    else
    {
        match->context = context;
    }
}

void ImgVwWindow::DestroySlideShowWindows()
{
    while (!slideshowwindows_.empty())
    {
        const auto window = slideshowwindows_.back();
        slideshowwindows_.pop_back();
        if (window != nullptr && IsWindow(window->hwnd()))
        {
            DestroyWindow(window->hwnd());
        }
    }
}

void ImgVwWindow::OnSlideShowWindowDestroyed(ImgVwWindow* window)
{
    slideshowwindows_.erase(std::remove(slideshowwindows_.begin(), slideshowwindows_.end(), window),
                            slideshowwindows_.end());
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

    if (multimonitorslideshowrunning_)
    {
        RestartMultiMonitorSlideShowTimer();
        return;
    }

    if (slideshowrunning_)
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        if (!slideshowwaitingforimage_)
        {
            SetTimer(hwnd_, IDT_SLIDESHOW, slideshowinterval_, nullptr);
        }
    }
}

void ImgVwWindow::IncreaseSlideShowSpeed()
{
    if (slideshowinterval_ > kMinimumSlideShowIntervalInMilliseconds)
    {
        slideshowinterval_ -= kSlideShowIntervalIncrementStepInMilliseconds;
        RestartSlideShowTimer();
        for (const auto window : slideshowwindows_)
        {
            window->slideshowinterval_ = slideshowinterval_;
        }
    }
}

void ImgVwWindow::DecreaseSlideShowSpeed()
{
    if (slideshowinterval_ < kMaximumSlideShowIntervalInMilliseconds)
    {
        slideshowinterval_ += kSlideShowIntervalIncrementStepInMilliseconds;
        RestartSlideShowTimer();
        for (const auto window : slideshowwindows_)
        {
            window->slideshowinterval_ = slideshowinterval_;
        }
    }
}

void ImgVwWindow::HandleSlideShow()
{
    if (multimonitorslideshowrunning_)
    {
        HandleMultiMonitorSlideShow();
        return;
    }

    if (slideshowrandom_)
    {
        AdvanceRandomSlide(TRUE);
        return;
    }

    if (browser_.MoveToNext() || browser_.MoveToFirst())
    {
        DisplayCurrentSlideWhenReady();
    }
}

BOOL ImgVwWindow::AdvanceRandomSlide(BOOL restarttimer)
{
    if (slideshowwaitingforimage_)
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

    slideshowneedsinitialadvance_ = FALSE;
    return TRUE;
}

BOOL ImgVwWindow::AdvanceSharedSlide(ImgVwWindow* target)
{
    return slideshowrandom_ ? AdvanceSharedRandomSlide(target) : AdvanceSharedSequentialSlide(target);
}

BOOL ImgVwWindow::AdvanceSharedSequentialSlide(ImgVwWindow* target)
{
    if (target == nullptr || target->slideshowwaitingforimage_)
    {
        return FALSE;
    }

    if (!multimonitorslideshowcursorpath_.empty())
    {
        browser_.MoveToItem(multimonitorslideshowcursorpath_);
    }

    if (!browser_.MoveToNext() && !browser_.MoveToFirst())
    {
        return FALSE;
    }

    const auto filepath = browser_.GetCurrentFilePath();
    multimonitorslideshowcursorpath_ = filepath;

    if (target == this)
    {
        target->DisplayCurrentSlideWithoutTimer();
        target->slideshowneedsinitialadvance_ = FALSE;
        return TRUE;
    }

    const auto displayed = target->DisplaySlidePath(filepath);
    RestoreSharedOwnerDisplayCursor();
    return displayed;
}

BOOL ImgVwWindow::AdvanceSharedRandomSlide(ImgVwWindow* target)
{
    if (target == nullptr || target->slideshowwaitingforimage_)
    {
        return FALSE;
    }

    std::vector<std::wstring> visiblepaths;
    if (!displayslidepath_.empty())
    {
        visiblepaths.push_back(displayslidepath_);
    }
    for (const auto window : slideshowwindows_)
    {
        if (window != nullptr && !window->displayslidepath_.empty())
        {
            visiblepaths.push_back(window->displayslidepath_);
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
        target->slideshowneedsinitialadvance_ = FALSE;
        return TRUE;
    }

    const auto displayed = target->DisplaySlidePath(filepath);
    RestoreSharedOwnerDisplayCursor();
    return displayed;
}

void ImgVwWindow::RestoreSharedOwnerDisplayCursor()
{
    if (!displayslidepath_.empty())
    {
        browser_.MoveToItem(displayslidepath_);
    }
}

BOOL ImgVwWindow::DisplaySlidePath(const std::wstring& filepath)
{
    if (filepath.empty() || !browser_.MoveToOrAddItem(filepath))
    {
        return FALSE;
    }

    DisplayCurrentSlideWithoutTimer();
    slideshowneedsinitialadvance_ = FALSE;
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
        displayslidepath_.clear();
        paintedslidepath_.clear();
        slideshowwaitingforimage_ = FALSE;
        slideshowneedsinitialadvance_ = slideshowrunning_ && slideshowrandom_;
        return;
    }

    slideshowneedsinitialadvance_ = FALSE;
    displayslidepath_ = browser_.GetCurrentFilePath();
    const auto status = imgitem->status();
    slideshowwaitingforimage_ = status != ImgItem::Status::Ready && status != ImgItem::Status::Error;
    UpdateLoadingProgressOverlayTimer();
    if (!slideshowwaitingforimage_)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ImgVwWindow::HandleStartupExitConditions()
{
    if (owner_ != nullptr || !browserinitialized_)
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
        ShowEmptyState(browsesubfolders_ ? L"No supported images were found in this folder or its subfolders."
                                         : L"No supported images were found in this folder.",
                       browsesubfolders_ ? FALSE : TRUE);
    }
    else if (IsEmptyStateVisible())
    {
        HideEmptyState();
        browseuistate_ = BrowseUiState::Viewing;
    }
}

void ImgVwWindow::HandleBrowserChanged()
{
    if (browserinitialized_ && IsEmptyStateVisible() && browser_.GetStats().found_images > 0)
    {
        HideEmptyState();
        browseuistate_ = BrowseUiState::Viewing;
    }
    HandleStartupExitConditions();
    if (owner_ == nullptr && multimonitorslideshowrunning_ && browser_.IsCollectingComplete())
    {
        const auto foundimages = browser_.GetStats().found_images;
        if (foundimages > multimonitorslideshowpreloadcount_)
        {
            multimonitorslideshowpreloadcount_ = PreloadMultiMonitorSlideShowContexts();
        }
    }
    UpdateLoadingProgressOverlayTimer();

    if (slideshowwaitingforimage_)
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

        slideshowwaitingforimage_ = FALSE;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (owner_ == nullptr && slideshowrunning_ && !multimonitorslideshowrunning_)
        {
            RestartSlideShowTimer();
        }

        return;
    }

    if (slideshowneedsinitialadvance_ && slideshowrunning_ && slideshowrandom_)
    {
        if (owner_ != nullptr)
        {
            owner_->AdvanceSharedSlide(this);
        }
        else if (multimonitorslideshowrunning_)
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

    const auto points = MAKEPOINTS(lParam);
    if (mousemovelastpoints_.x == 0 && mousemovelastpoints_.y == 0)
    {
        mousemovelastpoints_ = points;
    }
    else if (mousemovelastpoints_.x != points.x || mousemovelastpoints_.y != points.y)
    {
        QueryPerformanceCounter(&mousemovelastcounter_);
        mousemovelastpoints_ = points;

        if (!mousehidetimerstarted_)
        {
            ShowCursor(TRUE);
            SetTimer(hwnd_, IDT_HIDEMOUSE, kMouseHideIntervalInMilliseconds, nullptr);
            mousehidetimerstarted_ = TRUE;
        }
    }

    return FALSE;
}

void ImgVwWindow::HandleHideMouseCursor()
{
    if (IsEmptyStateVisible())
    {
        KillTimer(hwnd_, IDT_HIDEMOUSE);
        mousehidetimerstarted_ = FALSE;
        return;
    }

    KillTimer(hwnd_, IDT_HIDEMOUSE);

    LARGE_INTEGER counter, elapsed;
    QueryPerformanceCounter(&counter);
    elapsed.QuadPart = (counter.QuadPart - mousemovelastcounter_.QuadPart) * 1000;
    const auto elapsedmilliseconds = static_cast<UINT>(elapsed.QuadPart / qpcfrequency_.QuadPart);

    if (elapsedmilliseconds < kMouseHideIntervalInMilliseconds)
    {
        SetTimer(hwnd_, IDT_HIDEMOUSE, kMouseHideIntervalInMilliseconds - elapsedmilliseconds, nullptr);
    }
    else
    {
        ShowCursor(FALSE);
        mousehidetimerstarted_ = FALSE;
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

    ShowCursor(TRUE);
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

    ShowCursor(FALSE);
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
            ShowCursor(TRUE);
        }
        TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        if (restore_viewer_cursor)
        {
            ShowCursor(FALSE);
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
            ShowCursor(FALSE);

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
    DeleteObject(backgroundbrush_);
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
        ShowCursor(TRUE);
        DialogBox(hinst_, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd_, reinterpret_cast<DLGPROC>(AboutDialogProc));
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
        activeparam_ = LOWORD(wParam);
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
    }

    return Window::HandleMessage(uMsg, wParam, lParam);
}

#include "ImgVwWindow.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace
{
typedef UINT(WINAPI* GetDpiForWindowProc)(HWND hwnd);

constexpr UINT kDefaultDpi = 96;

struct OverlayColors
{
    COLORREF background;
    COLORREF panel;
    COLORREF border;
    COLORREF text;
};

OverlayColors GetOverlayColors(BOOL light_theme)
{
    return light_theme ? OverlayColors{RGB(245, 245, 245), RGB(255, 255, 255), RGB(140, 140, 140), RGB(50, 50, 50)}
                       : OverlayColors{RGB(0, 0, 0), RGB(0, 0, 0), RGB(90, 90, 90), RGB(180, 180, 180)};
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

std::wstring FormatByteSize(unsigned long long bytes)
{
    static const wchar_t* kUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    auto value = static_cast<double>(bytes);
    std::size_t unitindex{};
    while (value >= 1024.0 && unitindex < _countof(kUnits) - 1)
    {
        value /= 1024.0;
        ++unitindex;
    }

    std::wstringstream text;
    if (unitindex == 0)
    {
        text << bytes << L" " << kUnits[unitindex];
    }
    else if (value >= 100.0)
    {
        text << static_cast<unsigned long long>(value + 0.5) << L" " << kUnits[unitindex];
    }
    else
    {
        text.setf(std::ios::fixed);
        text.precision(1);
        text << value << L" " << kUnits[unitindex];
    }

    return text.str();
}

void WriteByteSizeColumn(std::wostream& text, unsigned long long bytes, std::streamsize width)
{
    constexpr std::streamsize kUnitWidth = 2;
    const auto formatted = FormatByteSize(bytes);
    const auto separator = formatted.rfind(L' ');
    const auto value = formatted.substr(0, separator);
    const auto unit = formatted.substr(separator + 1);
    const auto valuewidth = width - kUnitWidth - 1;

    text << std::right << std::setw(valuewidth) << value << L' ' << std::left << std::setw(kUnitWidth) << unit
         << std::right;
}

std::wstring FormatPercent(std::size_t numerator, std::size_t denominator)
{
    std::wstringstream text;
    text << (denominator > 0 ? numerator * 100 / denominator : 0) << L"%";
    return text.str();
}

bool ContainsRect(const RECT& outer, const RECT& inner)
{
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
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
    NONCLIENTMETRICS nonclientmetrics;
#if (WINVER >= 0x0600)
    nonclientmetrics.cbSize = sizeof(NONCLIENTMETRICS) - sizeof(nonclientmetrics.iPaddedBorderWidth);
#else
    nonclientmetrics.cbSize = sizeof(NONCLIENTMETRICS);
#endif
    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &nonclientmetrics, 0))
    {
        captionfont_ = CreateFontIndirect(&nonclientmetrics.lfMessageFont);
    }
    LoadEmptyStateLogo();

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
    CreateEmptyStateControls();
    if (owner_ == nullptr)
    {
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
        PaintEmptyState(pps);
        return;
    }

    const auto imgitem = browser_.GetCurrentItem();
    bool overlaydrawn = false;
    if (imgitem != nullptr)
    {
        const auto status = imgitem->status();
        if ((slideshowwaitingforimage_ || firstimagepaint_) && status != ImgItem::Status::Ready &&
            status != ImgItem::Status::Error)
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

        firstimagepaint_ = FALSE;

        if (IsInfoOverlayVisible() && imgitem->status() == ImgItem::Status::Ready &&
            !IsRectEmpty(&loaderstatsoverlayrect_) && ContainsRect(loaderstatsoverlayrect_, pps->rcPaint))
        {
            DrawLoaderStatsOverlay(pps->hdc, imgitem.get());
            return;
        }

        if (DisplayImage(pps->hdc, imgitem.get()))
        {
            paintedslidepath_ = browser_.GetCurrentFilePath();
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
    RestoreEmptyStateButtonFocus(openimagebutton_);

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
    RestoreEmptyStateButtonFocus(openfolderbutton_);

    if (restore_viewer_input && !IsEmptyStateVisible())
    {
        SetCapture(hwnd_);
        ShowCursor(FALSE);
    }
}

void ImgVwWindow::RestoreEmptyStateButtonFocus(HWND button)
{
    if (!IsEmptyStateVisible() || button == nullptr || !IsWindowVisible(button))
    {
        return;
    }

    SetFocus(button);
    if (GetFocus() == button)
    {
        InvalidateRect(button, nullptr, FALSE);
        UpdateWindow(button);
    }
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
        ShowSearchingSubfoldersState();
    }
}

void ImgVwWindow::LoadEmptyStateLogo()
{
    const auto resource = FindResource(hinst_, MAKEINTRESOURCE(IDR_WELCOME_LOGO), RT_RCDATA);
    if (resource == nullptr)
    {
        return;
    }

    const auto resource_size = SizeofResource(hinst_, resource);
    const auto resource_handle = LoadResource(hinst_, resource);
    const auto resource_data = resource_handle == nullptr ? nullptr : LockResource(resource_handle);
    if (resource_size == 0 || resource_data == nullptr)
    {
        return;
    }

    const auto image_memory = GlobalAlloc(GMEM_MOVEABLE, resource_size);
    if (image_memory == nullptr)
    {
        return;
    }

    const auto image_data = GlobalLock(image_memory);
    if (image_data == nullptr)
    {
        GlobalFree(image_memory);
        return;
    }

    std::memcpy(image_data, resource_data, resource_size);
    GlobalUnlock(image_memory);

    IStream* image_stream{};
    if (FAILED(CreateStreamOnHGlobal(image_memory, TRUE, &image_stream)))
    {
        GlobalFree(image_memory);
        return;
    }

    auto image = std::make_unique<Gdiplus::Image>(image_stream, FALSE);
    if (image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0 || image->GetHeight() == 0)
    {
        image.reset();
        image_stream->Release();
        return;
    }

    emptystatelogostream_ = image_stream;
    emptystatelogo_ = std::move(image);
}

void ImgVwWindow::CreateEmptyStateControls()
{
    if (owner_ != nullptr || openimagebutton_ != nullptr)
    {
        return;
    }

    const auto button_style = WS_CHILD | WS_TABSTOP | BS_OWNERDRAW;
    openimagebutton_ = CreateWindow(L"BUTTON", L"Open &image...", button_style, 0, 0, 0, 0, hwnd_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDM_OPEN_IMAGE)), hinst_, nullptr);
    openfolderbutton_ = CreateWindow(L"BUTTON", L"Open &folder...", button_style, 0, 0, 0, 0, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDM_OPEN_FOLDER)), hinst_, nullptr);
    searchsubfoldersbutton_ =
        CreateWindow(L"BUTTON", L"&Search subfolders", button_style, 0, 0, 0, 0, hwnd_,
                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDM_SEARCH_SUBFOLDERS)), hinst_, nullptr);
    exitbutton_ = CreateWindow(L"BUTTON", L"E&xit", button_style, 0, 0, 0, 0, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDM_EXIT)), hinst_, nullptr);

    for (const auto button : {openimagebutton_, openfolderbutton_, searchsubfoldersbutton_, exitbutton_})
    {
        if (button != nullptr && captionfont_ != nullptr)
        {
            SendMessage(button, WM_SETFONT, reinterpret_cast<WPARAM>(captionfont_), TRUE);
        }
    }
}

BOOL ImgVwWindow::IsEmptyStateVisible() const
{
    return browseuistate_ == BrowseUiState::Empty || browseuistate_ == BrowseUiState::NoImages ||
           browseuistate_ == BrowseUiState::SearchingSubfolders;
}

BOOL ImgVwWindow::IsSearchingSubfolders() const
{
    return browseuistate_ == BrowseUiState::SearchingSubfolders;
}

void ImgVwWindow::ShowEmptyState(const std::wstring& message, BOOL show_search_subfolders)
{
    if (owner_ != nullptr)
    {
        return;
    }

    const auto was_visible = IsEmptyStateVisible();
    browseuistate_ = browserinitialized_ ? BrowseUiState::NoImages : BrowseUiState::Empty;
    emptystatemessage_ = message;
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

    if (openimagebutton_ != nullptr)
    {
        ShowWindow(openimagebutton_, SW_SHOW);
    }
    if (openfolderbutton_ != nullptr)
    {
        ShowWindow(openfolderbutton_, SW_SHOW);
    }
    if (searchsubfoldersbutton_ != nullptr)
    {
        ShowWindow(searchsubfoldersbutton_, show_search_subfolders ? SW_SHOW : SW_HIDE);
    }
    if (exitbutton_ != nullptr)
    {
        ShowWindow(exitbutton_, SW_SHOW);
    }

    UpdateEmptyStateLayout();
    const auto focusbutton =
        show_search_subfolders && searchsubfoldersbutton_ != nullptr ? searchsubfoldersbutton_ : openimagebutton_;
    if (focusbutton != nullptr)
    {
        SetFocus(focusbutton);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImgVwWindow::ShowSearchingSubfoldersState()
{
    browseuistate_ = BrowseUiState::SearchingSubfolders;
    emptystatemessage_ = L"Searching subfolders for supported images...";
    for (const auto button : {openimagebutton_, openfolderbutton_, searchsubfoldersbutton_})
    {
        if (button != nullptr)
        {
            ShowWindow(button, SW_HIDE);
        }
    }
    if (exitbutton_ != nullptr)
    {
        ShowWindow(exitbutton_, SW_SHOW);
    }

    UpdateEmptyStateLayout();
    if (exitbutton_ != nullptr)
    {
        SetFocus(exitbutton_);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ImgVwWindow::HideEmptyState()
{
    if (!IsEmptyStateVisible())
    {
        return;
    }

    browseuistate_ = BrowseUiState::Collecting;
    for (const auto button : {openimagebutton_, openfolderbutton_, searchsubfoldersbutton_, exitbutton_})
    {
        if (button != nullptr)
        {
            ShowWindow(button, SW_HIDE);
        }
    }

    if (primarywindow_)
    {
        SetCapture(hwnd_);
        ShowCursor(FALSE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

ImgVwWindow::EmptyStateLayout ImgVwWindow::CalculateEmptyStateLayout(const RECT& client_rect) const
{
    EmptyStateLayout layout;
    const auto available_width = std::max(0, static_cast<INT>(client_rect.right) - ScaleForWindowDpi(48));
    layout.panel_width = std::min(available_width, ScaleForWindowDpi(460));
    layout.panel_height = ScaleForWindowDpi(browseuistate_ == BrowseUiState::NoImages ? 132 : 96);

    if (emptystatelogo_ != nullptr && emptystatelogo_->GetWidth() > 0 && emptystatelogo_->GetHeight() > 0)
    {
        layout.logo_width = layout.panel_width * 4 / 5;
        layout.logo_height = MulDiv(layout.logo_width, static_cast<INT>(emptystatelogo_->GetHeight()),
                                    static_cast<INT>(emptystatelogo_->GetWidth()));
    }

    const auto logo_gap = layout.logo_height > 0 ? ScaleForWindowDpi(12) : 0;
    const auto panel_button_gap = ScaleForWindowDpi(20);
    const auto button_height = ScaleForWindowDpi(28);
    const auto button_row_gap = ScaleForWindowDpi(12);
    const auto button_rows = IsSearchingSubfolders() ? 1 : 2;
    const auto content_height = layout.logo_height + logo_gap + layout.panel_height + panel_button_gap +
                                button_height * button_rows + button_row_gap * (button_rows - 1);
    const auto content_top = std::max(0, (static_cast<INT>(client_rect.bottom) - content_height) / 2);
    layout.buttons_top = content_top + layout.logo_height + logo_gap + layout.panel_height + panel_button_gap;
    return layout;
}

void ImgVwWindow::UpdateEmptyStateLayout()
{
    if (owner_ != nullptr || openimagebutton_ == nullptr || openfolderbutton_ == nullptr)
    {
        return;
    }

    RECT client_rect{};
    if (!GetClientRect(hwnd_, &client_rect))
    {
        return;
    }

    const auto layout = CalculateEmptyStateLayout(client_rect);
    const auto button_width = ScaleForWindowDpi(150);
    const auto button_height = ScaleForWindowDpi(28);
    const auto gap = ScaleForWindowDpi(12);
    const auto buttons_width = button_width * 2 + gap;
    const auto left = (client_rect.right - buttons_width) / 2;
    const auto top = layout.buttons_top;
    if (IsSearchingSubfolders())
    {
        if (exitbutton_ != nullptr)
        {
            MoveWindow(exitbutton_, (client_rect.right - button_width) / 2, top, button_width, button_height, TRUE);
        }
        return;
    }

    MoveWindow(openimagebutton_, left, top, button_width, button_height, TRUE);
    MoveWindow(openfolderbutton_, left + button_width + gap, top, button_width, button_height, TRUE);
    const auto secondary_top = top + button_height + gap;
    if (searchsubfoldersbutton_ != nullptr)
    {
        const auto search_visible = IsWindowVisible(searchsubfoldersbutton_);
        const auto search_left = search_visible ? left : (client_rect.right - button_width) / 2;
        MoveWindow(searchsubfoldersbutton_, search_left, secondary_top, button_width, button_height, TRUE);
        if (exitbutton_ != nullptr)
        {
            const auto exit_left = search_visible ? left + button_width + gap : (client_rect.right - button_width) / 2;
            MoveWindow(exitbutton_, exit_left, secondary_top, button_width, button_height, TRUE);
        }
    }
    else if (exitbutton_ != nullptr)
    {
        MoveWindow(exitbutton_, (client_rect.right - button_width) / 2, secondary_top, button_width, button_height,
                   TRUE);
    }
}

BOOL ImgVwWindow::TranslateEmptyStateDialogMessage(MSG* message) const
{
    return message != nullptr && IsEmptyStateVisible() && IsDialogMessage(hwnd_, message);
}

void ImgVwWindow::PaintEmptyState(PAINTSTRUCT* pps)
{
    RECT client_rect{};
    if (!GetClientRect(hwnd_, &client_rect))
    {
        return;
    }

    const auto colors = GetOverlayColors(systemlighttheme_);
    const auto background = CreateSolidBrush(colors.background);
    if (background != nullptr)
    {
        FillRect(pps->hdc, &client_rect, background);
        DeleteObject(background);
    }

    const auto layout = CalculateEmptyStateLayout(client_rect);
    const auto panel_width = layout.panel_width;
    const auto panel_height = layout.panel_height;
    const auto panel_left = (client_rect.right - panel_width) / 2;
    const auto panel_top = layout.buttons_top - ScaleForWindowDpi(20) - panel_height;
    const RECT panel_rect{panel_left, panel_top, panel_left + panel_width, panel_top + panel_height};

    const auto text = IsSearchingSubfolders()
                          ? emptystatemessage_
                          : emptystatemessage_ + L"\r\n\r\nYou can also drag an image or folder here.";
    DrawTextOverlay(pps->hdc, panel_rect, text, nullptr, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX, colors.background,
                    TRUE);

    if (emptystatelogo_ != nullptr && layout.logo_width > 0 && layout.logo_height > 0)
    {
        const auto logo_left = (client_rect.right - layout.logo_width) / 2;
        const auto logo_top = panel_top - layout.logo_height - ScaleForWindowDpi(12);
        Gdiplus::Graphics graphics(pps->hdc);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::ImageAttributes image_attributes;
        image_attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        const Gdiplus::Rect destination(logo_left, logo_top, layout.logo_width, layout.logo_height);
        graphics.DrawImage(emptystatelogo_.get(), destination, 0, 0, emptystatelogo_->GetWidth(),
                           emptystatelogo_->GetHeight(), Gdiplus::UnitPixel, &image_attributes);
    }
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
    UpdateEmptyStateLayout();
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
    UpdateEmptyStateLayout();
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

bool ImgVwWindow::DisplayImage(HDC dc, const ImgItem* item)
{
    if (item->status() != ImgItem::Status::Ready)
    {
        return false;
    }

    RECT windowrectangle{};
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return false;
    }

    const auto imgbitmap = item->GetDisplayBitmap();
    const ImgRenderInput input{dc,
                               backgroundbrush_,
                               windowrectangle,
                               imgbitmap.bitmap(),
                               item->offsetx(),
                               item->offsety(),
                               item->displaywidth(),
                               item->displayheight(),
                               IsInfoOverlayVisible() && !IsRectEmpty(&loaderstatsoverlayrect_),
                               loaderstatsoverlayrect_};
    return image_renderer_.Render(input).Succeeded();
}

bool ImgVwWindow::DisplayFileInformation(HDC dc, const RECT& paintrect, const ImgItem* item,
                                         const std::wstring& filepath)
{
    if (IsRectEmpty(&paintrect))
    {
        return false;
    }

    const auto overlaydrawn = IsInfoOverlayVisible();
    auto text = BuildItemInfoOverlayText(item, filepath);
    RECT overlayrect{};
    if (loaderstatsoverlayvisible_)
    {
        loaderstatsoverlaytext_ = BuildLoaderStatsOverlayText();
        loaderstatsoverlayrect_ = CalculateLoaderStatsOverlayRect(dc, loaderstatsoverlaytext_);
        text = loaderstatsoverlaytext_;
        overlayrect = loaderstatsoverlayrect_;
    }
    else
    {
        overlayrect = CalculateLoaderStatsOverlayRect(dc, text);
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
        DrawTextOverlay(dc, overlayrect, text, nullptr);
        return overlaydrawn;
    }

    const auto previousbitmap = SelectObject(memorydc, bitmap);
    if (previousbitmap == nullptr || previousbitmap == HGDI_ERROR)
    {
        DeleteObject(bitmap);
        DeleteDC(memorydc);
        FillRect(dc, &paintrect, backgroundbrush_);
        DrawTextOverlay(dc, overlayrect, text, nullptr);
        return overlaydrawn;
    }

    POINT previousorigin{};
    SetViewportOrgEx(memorydc, -paintrect.left, -paintrect.top, &previousorigin);
    FillRect(memorydc, &paintrect, backgroundbrush_);
    DrawTextOverlay(memorydc, overlayrect, text, nullptr);
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
    if (IsRectEmpty(&paintrect) || !IsLoadingProgressOverlayVisible(item))
    {
        return false;
    }

    const auto text = BuildItemInfoOverlayText(item, filepath);
    const auto overlayrect = CalculateLoaderStatsOverlayRect(dc, text);
    FillRect(dc, &paintrect, backgroundbrush_);
    DrawTextOverlay(dc, overlayrect, text, nullptr);
    return true;
}

std::wstring ImgVwWindow::BuildItemInfoOverlayText(const ImgItem* item, const std::wstring& filepath) const
{
    std::wstringstream text;
    if (item == nullptr || item->status() != ImgItem::Status::Ready)
    {
        auto percent = 0;
        if (item != nullptr && item->status() == ImgItem::Status::Loading)
        {
            percent = std::max(item->loadingprogresspercent(), 0);
        }

        text << L"[" << percent << L"%] ";
    }
    text << filepath;

    return text.str();
}

BOOL ImgVwWindow::IsLoadingProgressOverlayVisible(const ImgItem* item) const
{
    if (item == nullptr)
    {
        return FALSE;
    }

    const auto status = item->status();
    if (status == ImgItem::Status::Ready || status == ImgItem::Status::Error || loadingprogresswaitstarttick_ == 0)
    {
        return FALSE;
    }

    return GetTickCount() - loadingprogresswaitstarttick_ >= kLoadingProgressOverlayDebounceInMilliseconds;
}

void ImgVwWindow::UpdateLoadingProgressOverlayTimer()
{
    const auto item = browser_.GetCurrentItem();
    const auto waiting =
        item != nullptr && item->status() != ImgItem::Status::Ready && item->status() != ImgItem::Status::Error;
    if (!waiting)
    {
        if (loadingprogressoverlayvisible_)
        {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (IsInfoOverlayVisible())
        {
            RefreshLoaderStatsOverlay();
        }

        KillTimer(hwnd_, kLoadingProgressOverlayTimer);
        lastloadingprogresspercent_ = -2;
        loadingprogresswaitstarttick_ = 0;
        loadingprogressoverlayvisible_ = FALSE;
        loadingprogresspath_.clear();
        return;
    }

    const auto currentpath = browser_.GetCurrentFilePath();
    if (loadingprogresspath_ != currentpath)
    {
        loadingprogresspath_ = currentpath;
        lastloadingprogresspercent_ = -2;
        loadingprogresswaitstarttick_ = 0;
        loadingprogressoverlayvisible_ = FALSE;
    }

    if (loadingprogresswaitstarttick_ == 0)
    {
        loadingprogresswaitstarttick_ = GetTickCount();
    }

    SetTimer(hwnd_, kLoadingProgressOverlayTimer, kLoadingProgressOverlayIntervalInMilliseconds, nullptr);

    const auto percent = item->loadingprogresspercent();
    const auto visible = IsLoadingProgressOverlayVisible(item.get());
    if (percent != lastloadingprogresspercent_ || visible != loadingprogressoverlayvisible_)
    {
        lastloadingprogresspercent_ = percent;
        loadingprogressoverlayvisible_ = visible;
        if (IsInfoOverlayVisible())
        {
            RefreshLoaderStatsOverlay();
        }
        else if (visible)
        {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
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
    return loaderstatsoverlayvisible_ || IsFilenameOverlayVisible();
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
    if (loaderstatsoverlayvisible_ == visible)
    {
        return;
    }

    loaderstatsoverlayvisible_ = visible;
    if (loaderstatsoverlayvisible_)
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
    const auto previousrect = loaderstatsoverlayrect_;
    loaderstatsoverlaytext_.clear();
    SetRectEmpty(&loaderstatsoverlayrect_);
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

    std::wstringstream text;
    std::size_t cached{};
    std::size_t queued{};
    std::size_t freeslots{};
    std::size_t maximumslots{};
    for (const auto& target : targetstats)
    {
        const auto& size_stats = target.size;
        const auto size_total = size_stats.queued + size_stats.loading + size_stats.ready + size_stats.error;
        cached += size_total;
        queued += target.loader.queued;
        freeslots += target.loader.free_slots;
        maximumslots += target.loader.maximum_slots;
    }

    text << L"Found: " << sharedstats.found_images << L"; Cached: " << cached << L"; Queued: " << queued << L"; Slots: "
         << freeslots << L"/" << maximumslots;
    const auto temppath = ImgSettings::GetInstance().temppath();
    ULARGE_INTEGER freebytesavailable{};
    if (!temppath.empty() && GetDiskFreeSpaceEx(temppath.c_str(), &freebytesavailable, nullptr, nullptr))
    {
        text << L"; Free: " << FormatByteSize(freebytesavailable.QuadPart);
    }

    if (slideshowrunning)
    {
        auto progress = randomslideshow ? sharedstats.random : sharedstats.sequential;
        if (!randomslideshow && slideshowowner->multimonitorslideshowrunning_ &&
            !slideshowowner->multimonitorslideshowcursorpath_.empty())
        {
            progress = slideshowowner->browser_.GetSequentialProgress(slideshowowner->multimonitorslideshowcursorpath_);
        }
        text << L"\r\n--------------------------------------------------------------------------\r\n";
        text << L"Mode: " << (randomslideshow ? L"Random" : L"Sequential") << L" slideshow; Cycle: "
             << progress.position << L" / " << progress.total;
        if (progress.total > 0)
        {
            text << L" (" << FormatPercent(progress.position, progress.total) << L")";
        }
    }

    text << L"\r\n";
    text << L"--------------------------------------------------------------------------\r\n";
    text << std::left << std::setw(12) << L"Size" << std::right << std::setw(8) << L"Ready" << std::setw(10)
         << L"Loaded" << std::setw(10) << L"Loading" << std::setw(10) << L"Queued" << std::setw(8) << L"Errors"
         << std::setw(10) << L"Used" << std::setw(3) << L"" << L"\r\n";
    text << L"--------------------------------------------------------------------------\r\n";

    for (const auto& target : targetstats)
    {
        const auto& size_stats = target.size;
        const auto size_total = size_stats.queued + size_stats.loading + size_stats.ready + size_stats.error;
        std::wstringstream size;
        size << size_stats.targetwidth << L"x" << size_stats.targetheight;
        text << std::left << std::setw(12) << size.str() << std::right << std::setw(8)
             << FormatPercent(size_stats.ready, size_total) << std::setw(10) << size_stats.ready << std::setw(10)
             << size_stats.loading << std::setw(10) << size_stats.queued << std::setw(8) << size_stats.error;
        WriteByteSizeColumn(text, size_stats.temp_file_bytes, 13);
        text << L"\r\n";
    }

    const auto currentitem = browser_.GetCurrentItem();
    const auto currentpath = browser_.GetCurrentFilePath();
    if (!currentpath.empty())
    {
        text << L"--------------------------------------------------------------------------\r\n";
        text << BuildItemInfoOverlayText(currentitem.get(), currentpath);
    }

    return text.str();
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

    return kDefaultDpi;
}

INT ImgVwWindow::ScaleForWindowDpi(INT value) const
{
    return MulDiv(value, static_cast<INT>(GetWindowDpi()), static_cast<INT>(kDefaultDpi));
}

HFONT ImgVwWindow::GetLoaderStatsOverlayFont()
{
    const auto dpi = GetWindowDpi();
    if (loaderstatsoverlayfont_ != nullptr && loaderstatsoverlayfontdpi_ == dpi)
    {
        return loaderstatsoverlayfont_;
    }

    if (loaderstatsoverlayfont_ != nullptr)
    {
        DeleteObject(loaderstatsoverlayfont_);
        loaderstatsoverlayfont_ = nullptr;
    }

    LOGFONT logfont{};
    logfont.lfHeight = -MulDiv(10, static_cast<INT>(dpi), 72);
    logfont.lfWeight = FW_NORMAL;
    logfont.lfOutPrecision = OUT_TT_PRECIS;
    logfont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    BOOL fontsmoothing{};
    UINT fontsmoothingtype{};
    const auto cleartypeenabled = SystemParametersInfo(SPI_GETFONTSMOOTHING, 0, &fontsmoothing, 0) && fontsmoothing &&
                                  SystemParametersInfo(SPI_GETFONTSMOOTHINGTYPE, 0, &fontsmoothingtype, 0) &&
                                  fontsmoothingtype == FE_FONTSMOOTHINGCLEARTYPE;
    logfont.lfQuality = cleartypeenabled ? CLEARTYPE_NATURAL_QUALITY : ANTIALIASED_QUALITY;
    lstrcpy(logfont.lfFaceName, L"Lucida Console");

    loaderstatsoverlayfont_ = CreateFontIndirect(&logfont);
    loaderstatsoverlayfontdpi_ = dpi;
    return loaderstatsoverlayfont_;
}

void ImgVwWindow::ResetLoaderStatsOverlayLayout()
{
    if (loaderstatsoverlayfont_ != nullptr)
    {
        DeleteObject(loaderstatsoverlayfont_);
        loaderstatsoverlayfont_ = nullptr;
    }
    loaderstatsoverlayfontdpi_ = 0;
    loaderstatsoverlaytext_.clear();
    SetRectEmpty(&loaderstatsoverlayrect_);
}

RECT ImgVwWindow::CalculateLoaderStatsOverlayRect(HDC dc, const std::wstring& text) const
{
    const auto inset = ScaleForWindowDpi(16);
    const auto horizontalpadding = ScaleForWindowDpi(8);
    const auto verticalpadding = ScaleForWindowDpi(6);
    const auto horizontalmargin = inset;
    const auto verticalmargin = inset;
    const auto fallbackwidth = ScaleForWindowDpi(800);
    const auto fallbackheight = ScaleForWindowDpi(600);
    const auto availablewidth =
        std::max(1, (clientwidth_ > 0 ? clientwidth_ : fallbackwidth) - horizontalmargin * 2 - horizontalpadding * 2);
    const auto availableheight =
        std::max(1, (clientheight_ > 0 ? clientheight_ : fallbackheight) - verticalmargin * 2 - verticalpadding * 2);
    RECT textrect{0, 0, availablewidth, availableheight};
    const auto font = const_cast<ImgVwWindow*>(this)->GetLoaderStatsOverlayFont();
    const auto previousfont = font == nullptr ? nullptr : SelectObject(dc, font);
    DrawText(dc, text.c_str(), -1, &textrect, DT_CALCRECT | DT_LEFT | DT_NOPREFIX);
    if (previousfont != nullptr)
    {
        SelectObject(dc, previousfont);
    }

    const auto textwidth = std::min(availablewidth, static_cast<INT>(textrect.right - textrect.left));
    const auto textheight = std::min(availableheight, static_cast<INT>(textrect.bottom - textrect.top));
    RECT backgroundrect{horizontalmargin, verticalmargin, horizontalmargin + textwidth + horizontalpadding * 2,
                        verticalmargin + textheight + verticalpadding * 2};
    return backgroundrect;
}

void ImgVwWindow::RefreshLoaderStatsOverlay()
{
    if (!IsInfoOverlayVisible())
    {
        ClearInfoOverlay();
        return;
    }

    const auto currentitem = browser_.GetCurrentItem();
    const auto text = loaderstatsoverlayvisible_
                          ? BuildLoaderStatsOverlayText()
                          : BuildItemInfoOverlayText(currentitem.get(), browser_.GetCurrentFilePath());
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

    const auto rect = CalculateLoaderStatsOverlayRect(dc, text);
    ReleaseDC(hwnd_, dc);

    RECT invalidaterect = rect;
    if (!IsRectEmpty(&loaderstatsoverlayrect_))
    {
        UnionRect(&invalidaterect, &invalidaterect, &loaderstatsoverlayrect_);
    }

    if (loaderstatsoverlaytext_ == text && EqualRect(&loaderstatsoverlayrect_, &rect))
    {
        return;
    }

    loaderstatsoverlaytext_ = text;
    loaderstatsoverlayrect_ = rect;
    InvalidateRect(hwnd_, &invalidaterect, FALSE);
}

void ImgVwWindow::DrawTextOverlay(HDC dc, const RECT& overlayrect, const std::wstring& text, const ImgItem* item,
                                  UINT textformat, COLORREF fallbackbackground, BOOL vertically_center_text)
{
    if (text.empty() || IsRectEmpty(&overlayrect))
    {
        return;
    }

    const auto overlaywidth = overlayrect.right - overlayrect.left;
    const auto overlayheight = overlayrect.bottom - overlayrect.top;
    const auto memorydc = CreateCompatibleDC(dc);
    const auto bitmap = memorydc == nullptr ? nullptr : CreateCompatibleBitmap(dc, overlaywidth, overlayheight);
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
        return;
    }

    const auto previousbitmap = SelectObject(memorydc, bitmap);
    if (previousbitmap == nullptr || previousbitmap == HGDI_ERROR)
    {
        DeleteObject(bitmap);
        DeleteDC(memorydc);
        return;
    }

    RECT backgroundrect{0, 0, overlaywidth, overlayheight};
    const auto colors = GetOverlayColors(systemlighttheme_);
    const auto fallback_background = CreateSolidBrush(fallbackbackground);
    if (fallback_background != nullptr)
    {
        FillRect(memorydc, &backgroundrect, fallback_background);
        DeleteObject(fallback_background);
    }

    if (item != nullptr && item->status() == ImgItem::Status::Ready)
    {
        RECT imagerect{item->offsetx(), item->offsety(), item->offsetx() + item->displaywidth(),
                       item->offsety() + item->displayheight()};
        RECT intersection{};
        if (IntersectRect(&intersection, &overlayrect, &imagerect))
        {
            const auto imgbitmap = item->GetDisplayBitmap();
            const auto sourcedc = CreateCompatibleDC(dc);
            if (sourcedc != nullptr)
            {
                const auto previoussourcebitmap = SelectObject(sourcedc, imgbitmap.bitmap());
                if (previoussourcebitmap != nullptr && previoussourcebitmap != HGDI_ERROR)
                {
                    BitBlt(memorydc, intersection.left - overlayrect.left, intersection.top - overlayrect.top,
                           intersection.right - intersection.left, intersection.bottom - intersection.top, sourcedc,
                           intersection.left - imagerect.left, intersection.top - imagerect.top, SRCCOPY);
                    SelectObject(sourcedc, previoussourcebitmap);
                }

                DeleteDC(sourcedc);
            }
        }
    }

    const auto paneldc = CreateCompatibleDC(dc);
    const auto panelbitmap = paneldc == nullptr ? nullptr : CreateCompatibleBitmap(dc, overlaywidth, overlayheight);
    if (paneldc != nullptr && panelbitmap != nullptr)
    {
        const auto previouspanelbitmap = SelectObject(paneldc, panelbitmap);
        const auto panel_brush = CreateSolidBrush(colors.panel);
        if (panel_brush != nullptr)
        {
            FillRect(paneldc, &backgroundrect, panel_brush);
            DeleteObject(panel_brush);
        }

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 128;
        AlphaBlend(memorydc, 0, 0, overlaywidth, overlayheight, paneldc, 0, 0, overlaywidth, overlayheight, blend);

        SelectObject(paneldc, previouspanelbitmap);
    }

    if (panelbitmap != nullptr)
    {
        DeleteObject(panelbitmap);
    }
    if (paneldc != nullptr)
    {
        DeleteDC(paneldc);
    }

    const auto borderpen = CreatePen(PS_SOLID, 1, colors.border);
    if (borderpen != nullptr)
    {
        const auto previouspen = SelectObject(memorydc, borderpen);
        const auto previousbrush = SelectObject(memorydc, GetStockObject(NULL_BRUSH));
        Rectangle(memorydc, 0, 0, overlaywidth, overlayheight);
        SelectObject(memorydc, previousbrush);
        SelectObject(memorydc, previouspen);
        DeleteObject(borderpen);
    }

    SetBkMode(memorydc, TRANSPARENT);
    SetTextColor(memorydc, colors.text);
    const auto horizontalpadding = ScaleForWindowDpi(8);
    const auto verticalpadding = ScaleForWindowDpi(6);
    RECT textrect{horizontalpadding, verticalpadding, overlaywidth - horizontalpadding,
                  overlayheight - verticalpadding};
    const auto overlayfont = GetLoaderStatsOverlayFont();
    const auto previousfont = overlayfont == nullptr ? nullptr : SelectObject(memorydc, overlayfont);
    if (vertically_center_text)
    {
        RECT measured_text{0, 0, textrect.right - textrect.left, 0};
        DrawText(memorydc, text.c_str(), -1, &measured_text, textformat | DT_CALCRECT);
        const auto available_height = textrect.bottom - textrect.top;
        const auto text_height = measured_text.bottom - measured_text.top;
        if (text_height < available_height)
        {
            textrect.top += (available_height - text_height) / 2;
            textrect.bottom = textrect.top + text_height;
        }
    }

    DrawText(memorydc, text.c_str(), -1, &textrect, textformat);
    if (overlayfont != nullptr)
    {
        SelectObject(memorydc, previousfont);
    }

    BitBlt(dc, overlayrect.left, overlayrect.top, overlaywidth, overlayheight, memorydc, 0, 0, SRCCOPY);
    SelectObject(memorydc, previousbitmap);
    DeleteObject(bitmap);
    DeleteDC(memorydc);
}

void ImgVwWindow::DrawLoaderStatsOverlay(HDC dc, const ImgItem* item)
{
    DrawTextOverlay(dc, loaderstatsoverlayrect_, loaderstatsoverlaytext_, item);
}

void ImgVwWindow::DrawEmptyStateButton(const DRAWITEMSTRUCT* drawitem)
{
    if (drawitem == nullptr || drawitem->CtlType != ODT_BUTTON)
    {
        return;
    }

    wchar_t text[64]{};
    GetWindowText(drawitem->hwndItem, text, _countof(text));
    const auto colors = GetOverlayColors(systemlighttheme_);
    auto text_format = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
    if ((drawitem->itemState & ODS_NOACCEL) != 0)
    {
        text_format |= DT_HIDEPREFIX;
    }
    DrawTextOverlay(drawitem->hDC, drawitem->rcItem, text, nullptr, text_format, colors.background);
    if ((drawitem->itemState & ODS_FOCUS) != 0)
    {
        const auto focus_brush = CreateSolidBrush(RGB(123, 104, 238));
        if (focus_brush != nullptr)
        {
            auto focus_rect = drawitem->rcItem;
            const auto focus_width = std::max(2, ScaleForWindowDpi(3));
            for (INT inset = 0; inset < focus_width && !IsRectEmpty(&focus_rect); ++inset)
            {
                FrameRect(drawitem->hDC, &focus_rect, focus_brush);
                InflateRect(&focus_rect, -1, -1);
            }
            DeleteObject(focus_brush);
        }
    }
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
    if (!ImgItem::ResetDefaultICCProfile())
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

    std::vector<std::shared_ptr<ImgBrowserLoadContext>> preloadedcontexts;
    for (const auto window : slideshowwindows_)
    {
        if (window != nullptr)
        {
            window->UpdateInfoOverlayForWindow();
            const auto context = window->browser_.loadcontext();
            if (std::find(preloadedcontexts.begin(), preloadedcontexts.end(), context) == preloadedcontexts.end())
            {
                window->browser_.PreloadFrom(browser_);
                preloadedcontexts.push_back(context);
            }
        }
    }

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
    if (owner_ != nullptr || !browserinitialized_ || !browser_.IsCollectingComplete())
    {
        return;
    }

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
        if (!ImgItem::IsCMYKICCProfile(ofn.lpstrFile))
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
    browser_.StopBrowsing();
    DeleteObject(backgroundbrush_);
    DeleteObject(captionfont_);
    emptystatelogo_.reset();
    if (emptystatelogostream_ != nullptr)
    {
        emptystatelogostream_->Release();
        emptystatelogostream_ = nullptr;
    }
    DeleteObject(loaderstatsoverlayfont_);
    if (owner_ != nullptr)
    {
        owner_->OnSlideShowWindowDestroyed(this);
    }
}

LRESULT ImgVwWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case kBrowserChangedMessage:
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
        if (owner_ != nullptr)
        {
            return SendMessage(CommandTarget()->hwnd(), uMsg, wParam, lParam);
        }

        switch (LOWORD(wParam))
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
            if (IsEmptyStateVisible())
            {
                const auto focused_window = GetFocus();
                const auto focused_button = focused_window == openimagebutton_ || focused_window == openfolderbutton_ ||
                                            focused_window == searchsubfoldersbutton_ || focused_window == exitbutton_;
                SendMessage(focused_button ? focused_window : openimagebutton_, BM_CLICK, 0, 0);
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
            DeleteCurrentItem(LOWORD(wParam) == IDR_RECYCLE);
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
    case WM_DRAWITEM:
        if (wParam == IDM_OPEN_IMAGE || wParam == IDM_OPEN_FOLDER || wParam == IDM_SEARCH_SUBFOLDERS ||
            wParam == IDM_EXIT)
        {
            DrawEmptyStateButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
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
        if (BeginWindowDrag(lParam))
        {
            return 0;
        }

        break;
    case WM_LBUTTONUP:
        FinishWindowDrag();
        return 0;
    case WM_MOUSEMOVE:
        if (UpdateWindowDrag(wParam, lParam))
        {
            return 0;
        }

        if (HandleMouseMove(wParam, lParam))
        {
            return 0;
        }

        break;
    case WM_MOUSEWHEEL:
        HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));

        return FALSE;
    case WM_SYSCOMMAND:
        if (LOWORD(wParam) == SC_CLOSE)
        {
            CloseWindow();
        }

        return FALSE;
    case WM_TIMER:
        switch (wParam)
        {
        case IDT_SLIDESHOW:
            HandleSlideShow();
            return 0;
        case IDT_HIDEMOUSE:
            HandleHideMouseCursor();
            return 0;
        case kLoaderStatsOverlayTimer:
            UpdateLoaderStatsOverlayVisibility();
            if (loaderstatsoverlayvisible_)
            {
                RefreshLoaderStatsOverlay();
            }
            return 0;
        case kLoadingProgressOverlayTimer:
            UpdateLoadingProgressOverlayTimer();
            return 0;
        }

        break;
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

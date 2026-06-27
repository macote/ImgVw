#include "ImgVwWindow.h"

#include <algorithm>
#include <climits>

struct ImgVwWindow::MonitorCreateContext
{
    HINSTANCE hinst{};
    std::wstring path;
    HMONITOR primarymonitor{};
    ImgVwWindow* owner{};
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
        window->slideshowrandom_ = TRUE;
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

    QueryPerformanceFrequency(&qpcfrequency_);
    arrowcursor_ = LoadCursor(nullptr, IDC_ARROW);
    SetCursor(arrowcursor_);
    if (primarywindow_)
    {
        SetCapture(hwnd_);
        ShowCursor(FALSE);
    }

    InitializeMonitorState();
    InitializeBrowser(path_);

    return FALSE;
}

void ImgVwWindow::PaintContent(PAINTSTRUCT* pps)
{
    const auto imgitem = browser_.GetCurrentItem();
    if (imgitem != nullptr)
    {
        if (slideshowwaitingforimage_ && imgitem->status() != ImgItem::Status::Ready &&
            imgitem->status() != ImgItem::Status::Error)
        {
            return;
        }

        if (!DisplayImage(pps->hdc, imgitem.get()))
        {
            DisplayFileInformation(pps->hdc, browser_.GetCurrentFilePath());
        }
    }
}

void ImgVwWindow::InitializeBrowser(const std::wstring& path)
{
    RECT windowrectangle{};
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return;
    }

    browser_.SetNotificationWindow(hwnd_, kBrowserChangedMessage);
    UpdateClientSize(windowrectangle.right, windowrectangle.bottom);
    browser_.BrowseAsync(path_, windowrectangle.right, windowrectangle.bottom);
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
}

void ImgVwWindow::InitializeMonitorState()
{
    currentmonitor_ = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
}

void ImgVwWindow::HandleWindowPosChanged()
{
    const auto monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr || monitor == currentmonitor_)
    {
        return;
    }

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
                               item->displayheight()};
    return image_renderer_.Render(input).Succeeded();
}

void ImgVwWindow::DisplayFileInformation(HDC dc, const std::wstring& filepath)
{
    RECT windowrectangle;
    GetWindowRect(hwnd_, &windowrectangle);
    FillRect(dc, &windowrectangle, backgroundbrush_);
    const auto text_length =
        static_cast<int>(filepath.size() > static_cast<size_t>(INT_MAX) ? INT_MAX : filepath.size());
    TextOut(dc, 0, 0, filepath.c_str(), text_length);
}

void ImgVwWindow::InvalidateScreen()
{
    slideshowwaitingforimage_ = FALSE;
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (slideshowrunning_)
    {
        RestartSlideShowTimer();
    }
}

void ImgVwWindow::PerformAction()
{
    const auto filepath = browser_.GetCurrentFilePath();
    ShowCursor(TRUE);
    MessageBox(hwnd_, filepath.c_str(), L"ImgVw", 0);
    ShowCursor(FALSE);
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
    if (distance & 0x8000)
    {
        BrowseNext();
    }
    else
    {
        BrowsePrevious();
    }
}

void ImgVwWindow::ToggleSlideShow(BOOL slideshowrandom)
{
    StopMultiMonitorRandomSlideShow();
    slideshowrandom_ = slideshowrandom;
    if (!slideshowrunning_)
    {
        StartSlideShow();
    }
    else
    {
        StopSlideShow();
    }
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

void ImgVwWindow::ToggleMultiMonitorRandomSlideShow()
{
    if (multimonitorslideshowrunning_)
    {
        StopMultiMonitorRandomSlideShow();
        return;
    }

    StartMultiMonitorRandomSlideShow();
}

void ImgVwWindow::StartMultiMonitorRandomSlideShow()
{
    StopSlideShow();
    DestroySlideShowWindows();

    slideshowrandom_ = TRUE;
    slideshowrunning_ = TRUE;
    multimonitorslideshowrunning_ = TRUE;
    multimonitorslideshowindex_ = 0;
    slideshowneedsinitialadvance_ = FALSE;

    const auto primarymonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    currentmonitor_ = primarymonitor;
    MonitorCreateContext context{hinst_, path_, primarymonitor, this};
    EnumDisplayMonitors(nullptr, nullptr, CreateSlideShowWindowForMonitor, reinterpret_cast<LPARAM>(&context));

    for (std::size_t index = 0; index < MultiMonitorSlideShowWindowCount(); ++index)
    {
        AdvanceSharedRandomSlide(MultiMonitorSlideShowWindowAt(index));
    }

    RestartMultiMonitorSlideShowTimer();
}

void ImgVwWindow::StopMultiMonitorRandomSlideShow()
{
    if (!multimonitorslideshowrunning_ && slideshowwindows_.empty())
    {
        return;
    }

    multimonitorslideshowrunning_ = FALSE;
    multimonitorslideshowindex_ = 0;
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
        AdvanceSharedRandomSlide(window);
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

BOOL ImgVwWindow::AdvanceSharedRandomSlide(ImgVwWindow* target)
{
    if (target == nullptr || target->slideshowwaitingforimage_)
    {
        return FALSE;
    }

    std::wstring filepath;
    BOOL foundpath = FALSE;
    const auto maxattempts = MultiMonitorSlideShowWindowCount() * 4 + 8;
    for (std::size_t attempt = 0; attempt < maxattempts; ++attempt)
    {
        if (!browser_.MoveToRandom())
        {
            RestoreSharedRandomOwnerDisplayCursor();
            return FALSE;
        }

        filepath = browser_.GetCurrentFilePath();
        if (!filepath.empty() && !IsSlidePathVisible(filepath))
        {
            foundpath = TRUE;
            break;
        }
    }

    if (!foundpath)
    {
        RestoreSharedRandomOwnerDisplayCursor();
        return FALSE;
    }

    if (target == this)
    {
        target->DisplayCurrentSlideWithoutTimer();
        target->slideshowneedsinitialadvance_ = FALSE;
        return TRUE;
    }

    const auto displayed = target->DisplaySlidePath(filepath);
    RestoreSharedRandomOwnerDisplayCursor();
    return displayed;
}

void ImgVwWindow::RestoreSharedRandomOwnerDisplayCursor()
{
    if (!displayslidepath_.empty())
    {
        browser_.MoveToItem(displayslidepath_);
    }
}

BOOL ImgVwWindow::IsSlidePathVisible(const std::wstring& filepath)
{
    if (filepath.empty())
    {
        return FALSE;
    }

    if (displayslidepath_ == filepath)
    {
        return TRUE;
    }

    for (const auto window : slideshowwindows_)
    {
        if (window != nullptr && window->displayslidepath_ == filepath)
        {
            return TRUE;
        }
    }

    return FALSE;
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
        slideshowwaitingforimage_ = FALSE;
        slideshowneedsinitialadvance_ = slideshowrunning_ && slideshowrandom_;
        return;
    }

    slideshowneedsinitialadvance_ = FALSE;
    displayslidepath_ = browser_.GetCurrentFilePath();
    const auto status = imgitem->status();
    slideshowwaitingforimage_ = status != ImgItem::Status::Ready && status != ImgItem::Status::Error;
    if (!slideshowwaitingforimage_)
    {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ImgVwWindow::HandleBrowserChanged()
{
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
            owner_->AdvanceSharedRandomSlide(this);
        }
        else if (multimonitorslideshowrunning_)
        {
            AdvanceSharedRandomSlide(this);
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

        ShowCursor(TRUE);
        TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        ShowCursor(FALSE);

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
    browser_.StopBrowsing();
    DeleteObject(backgroundbrush_);
    DeleteObject(captionfont_);
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
    case WM_WINDOWPOSCHANGED:
        HandleWindowPosChanged();
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
        case IDM_ABOUT:
            ShowCursor(TRUE);
            DialogBox(hinst_, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd_, reinterpret_cast<DLGPROC>(AboutDialogProc));
            break;
        case IDR_ENTER:
            PerformAction();
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
            ToggleMultiMonitorRandomSlideShow();
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
    case WM_CONTEXTMENU:
        HandleContextMenu(lParam);
        return TRUE;
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

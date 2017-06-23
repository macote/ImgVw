#include "ImgVwWindow.h"

ImgVwWindow* ImgVwWindow::Create(HINSTANCE hInst, std::vector<std::wstring> args)
{
    auto self = new ImgVwWindow(hInst, args);
    if (self != NULL)
    {
        self->backgroundbrush_ = CreateSolidBrush(RGB(0, 0, 0));
        self->manualcursor_ = TRUE;
        self->dontfillbackground_ = TRUE;
        if (self->WinCreateWindow(WS_EX_APPWINDOW, L"ImgVw", WS_POPUP, 0, 0, 
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL))
        {
            return self;
        }

        delete self;
    }

    return NULL;
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

    ShowCursor(FALSE);

    QueryPerformanceFrequency(&frequency_);

    InitializeBrowser(path_);

    return FALSE;
}

void ImgVwWindow::PaintContent(PAINTSTRUCT* pps)
{
    auto imgitem = browser_.GetCurrentItem();
    if (imgitem != nullptr)
    {
        if (!DisplayImage(pps->hdc, imgitem))
        {
            DisplayFileInformation(pps->hdc, browser_.GetCurrentFilePath());
        }

        if (WaitForSingleObject(imgitem->loadedevent(), INFINITE) != WAIT_OBJECT_0)
        {
            // TODO: handle error
        }
    }
}

void ImgVwWindow::InitializeBrowser(std::wstring path)
{
    RECT windowrectangle;
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return;
    }

    browser_.StartBrowsingAsync(path_, windowrectangle.right, windowrectangle.bottom);
}

BOOL ImgVwWindow::DisplayImage(HDC dc, ImgItem* item)
{
    if (item->status() != ImgItem::Status::Ready)
    {
        return FALSE;
    }

    RECT windowrectangle;
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return FALSE;
    }

    auto memorydc = CreateCompatibleDC(dc);
    auto imgbitmap = item->GetDisplayBitmap();
    auto replacedobject = SelectObject(memorydc, imgbitmap.bitmap());

    if (ExcludeClipRect(dc, item->offsetx(), item->offsety(), item->offsetx() + item->displaywidth(), item->offsety() + item->displayheight()) == RGN_ERROR)
    {
        // TODO: handle error
    }
    else if (FillRect(dc, &windowrectangle, backgroundbrush_) == 0)
    {
        // TODO: handle error
    }
    else if (SelectClipRgn(dc, NULL) == RGN_ERROR)
    {
        // TODO: handle error
    }
    else if (!BitBlt(dc, item->offsetx(), item->offsety(), item->displaywidth(), item->displayheight(), memorydc, 0, 0, SRCCOPY))
    {
        // TODO: handle error
    }

    SelectObject(memorydc, replacedobject);
    DeleteDC(memorydc);

    return TRUE;
}

void ImgVwWindow::DisplayFileInformation(HDC dc, std::wstring filepath)
{
    RECT windowrectangle;
    GetWindowRect(hwnd_, &windowrectangle);
    FillRect(dc, &windowrectangle, backgroundbrush_);
    TextOut(dc, 0, 0, filepath.c_str(), filepath.size());
}

void ImgVwWindow::InvalidateScreen()
{
    InvalidateRect(hwnd_, NULL, FALSE);
    if (slideshowrunning_)
    {
        RestartSlideShowTimer();
    }
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

void ImgVwWindow::ToggleSlideShow()
{
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
        SetTimer(hwnd_, IDT_SLIDESHOW, slideshowinterval_, NULL);
        slideshowrunning_ = TRUE;
    }
}

void ImgVwWindow::StopSlideShow()
{
    if (slideshowrunning_)
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        slideshowrunning_ = FALSE;
    }
}

void ImgVwWindow::RestartSlideShowTimer()
{
    if (slideshowrunning_)
    {
        KillTimer(hwnd_, IDT_SLIDESHOW);
        SetTimer(hwnd_, IDT_SLIDESHOW, slideshowinterval_, NULL);
    }
}

void ImgVwWindow::IncreaseSlideShowSpeed()
{
    if (slideshowinterval_ > kMinimumSlideShowIntervalInMilliseconds)
    {
        slideshowinterval_ -= kSlideShowIntervalIncrementStepInMilliseconds;
        RestartSlideShowTimer();
    }
}

void ImgVwWindow::DecreaseSlideShowSpeed()
{
    if (slideshowinterval_ < kMaximumSlideShowIntervalInMilliseconds)
    {
        slideshowinterval_ += kSlideShowIntervalIncrementStepInMilliseconds;
        RestartSlideShowTimer();
    }
}

void ImgVwWindow::HandleSlideShow()
{
    if (browser_.MoveToNext() || browser_.MoveToFirst())
    { 
        InvalidateScreen();
    }
}

void ImgVwWindow::DeleteCurrentItem(BOOL allowundo)
{
    if (browser_.GetCurrentItem() == nullptr)
    {
        return;
    }

    SHFILEOPSTRUCT shfileopstruct{};
    TCHAR deletepaths[4096];
    size_t pathlen = browser_.GetCurrentFilePath().size();
    wcsncpy(deletepaths, browser_.GetCurrentFilePath().c_str(), pathlen);
    deletepaths[pathlen + 1] = 0;
    shfileopstruct.hwnd = hwnd_;
    shfileopstruct.wFunc = FO_DELETE;
    shfileopstruct.pFrom = deletepaths;
    shfileopstruct.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI
        | FOF_NOCONFIRMMKDIR | FOF_WANTNUKEWARNING;
    if (allowundo)
    {
        shfileopstruct.fFlags |= FOF_ALLOWUNDO;
    }

    if (!SHFileOperation(&shfileopstruct) && !shfileopstruct.fAnyOperationsAborted)
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
    StopSlideShow();
    browser_.StopBrowsing();
    DeleteObject(backgroundbrush_);
    DeleteObject(captionfont_);
}

LRESULT ImgVwWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#if _DEBUG
    logger_.WriteLine(DebugHelper::FormatWindowMessage(uMsg, wParam, lParam));
#endif

    switch (uMsg)
    {
    case WM_ACTIVATE:
        activeparam_ = LOWORD(wParam);
        return FALSE;
    case WM_CREATE:
        return OnCreate();
    case WM_ERASEBKGND:
        return TRUE;
    case WM_SIZE:
    case WM_SETFOCUS:
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_ABOUT:
            ShowCursor(TRUE);
            DialogBox(hinst_, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd_, (DLGPROC)AboutDialogProc);
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
            ToggleSlideShow();
            break;
        case IDR_INCSSS:
            IncreaseSlideShowSpeed();
            break;
        case IDR_DECSSS:
            DecreaseSlideShowSpeed();
            break;
        case IDM_EXIT:
        case IDR_ESCAPE:
            CloseWindow();
            break;
        }

        return FALSE;
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
        }
        break;
    case WM_NCDESTROY:
        OnNCDestroy();
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return Window::HandleMessage(uMsg, wParam, lParam);
}

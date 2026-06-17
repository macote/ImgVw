#include "ImgVwWindow.h"

ImgVwWindow* ImgVwWindow::Create(HINSTANCE hInst, const std::vector<std::wstring> args)
{
    auto self = new ImgVwWindow(hInst, args);
    if (self != nullptr)
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

    return nullptr;
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
    arrowcursor_ = LoadCursor(NULL, IDC_ARROW);
    SetCursor(arrowcursor_);
    SetCapture(hwnd_);
    ShowCursor(FALSE);
    InitializeBrowser(path_);

    return FALSE;
}

void ImgVwWindow::PaintContent(PAINTSTRUCT* pps)
{
    const auto imgitem = browser_.GetCurrentItem();
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

void ImgVwWindow::InitializeBrowser(const std::wstring& path)
{
    RECT windowrectangle{};
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return;
    }

    browser_.BrowseAsync(path_, windowrectangle.right, windowrectangle.bottom);
}

BOOL ImgVwWindow::DisplayImage(HDC dc, const ImgItem* item)
{
    if (item->status() != ImgItem::Status::Ready)
    {
        if (item->iccprofileloadfailed())
        {
            SelectDefaultICCProfile();
            browser_.ReloadCurrentItem();

            if (item->status() != ImgItem::Status::Ready)
            {
                return FALSE;
            }
        }
        else
        {
            return FALSE;
        }
    }

    RECT windowrectangle{};
    if (!GetClientRect(hwnd_, &windowrectangle))
    {
        return FALSE;
    }

    const auto memorydc = CreateCompatibleDC(dc);
    const auto imgbitmap = item->GetDisplayBitmap();
    const auto replacedobject = SelectObject(memorydc, imgbitmap.bitmap());

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

void ImgVwWindow::DisplayFileInformation(HDC dc, const std::wstring& filepath)
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

void ImgVwWindow::PerformAction()
{
    const auto filepath = browser_.GetCurrentFilePath();
    ShowCursor(TRUE);
    MessageBox(hwnd_, filepath.c_str(), L"ImgVw", 0);
    ShowCursor(FALSE);
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
    auto invalidate = browser_.GetCurrentItem() == nullptr;
    browser_.BrowseSubFoldersAsync();
    if (invalidate)
    {
        browser_.GetReady();
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

void ImgVwWindow::ToggleSlideShow(BOOL slideshowrandom)
{
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
    if ((slideshowrandom_ && browser_.MoveToRandom()) || browser_.MoveToNext() || browser_.MoveToFirst())
    {
        InvalidateScreen();
    }
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
            SetTimer(hwnd_, IDT_HIDEMOUSE, kMouseHideIntervalInMilliseconds, NULL);
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
        SetTimer(hwnd_, IDT_HIDEMOUSE, kMouseHideIntervalInMilliseconds - elapsedmilliseconds, NULL);
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

    SHFILEOPSTRUCT shfileopstruct{};
    TCHAR deletepaths[4096] = {};
    const size_t pathlen = browser_.GetCurrentFilePath().size();
    wcsncpy(deletepaths, browser_.GetCurrentFilePath().c_str(), pathlen);
    deletepaths[pathlen + 1] = 0;
    deletepaths[pathlen] = 0;
    shfileopstruct.hwnd = hwnd_;
    shfileopstruct.wFunc = FO_DELETE;
    shfileopstruct.pFrom = deletepaths;
    shfileopstruct.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR | FOF_WANTNUKEWARNING;
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

void ImgVwWindow::SelectDefaultICCProfile()
{
    OPENFILENAME ofn;
    TCHAR szFile[260];

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = L"ICC Profile\0*.icc\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Select default ICC profile...";
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    ShowCursor(TRUE);
    if (GetOpenFileName(&ofn))
    {
        TCHAR appdatapath[260];
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdatapath)))
        {
            TCHAR imgvwappdatapath[260];
            PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath);

            auto result = SHCreateDirectoryEx(hwnd_, imgvwappdatapath, NULL);
            if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS)
            {
                TCHAR iccpath[260];
                PathCombine(iccpath, imgvwappdatapath, ImgItem::kDefaultICCProfileFilename);
                CopyFile(ofn.lpstrFile, iccpath, FALSE);
            }
        }
    }

    ShowCursor(FALSE);
}

void ImgVwWindow::HandleContextMenu(LPARAM lParam)
{
    RECT rc;
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    GetClientRect(hwnd_, &rc);
    ScreenToClient(hwnd_, &pt);

    if (PtInRect(&rc, pt))
    {
        ClientToScreen(hwnd_, &pt);

        const auto root = LoadMenu(hinst_, L"IMGPOPUP");
        const auto popup = GetSubMenu(root, 0);

        ShowCursor(TRUE);
        TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, NULL);
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
    StopSlideShow();
    browser_.StopBrowsing();
    DeleteObject(backgroundbrush_);
    DeleteObject(captionfont_);
}

LRESULT ImgVwWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#if _DEBUG && LOGIMGVWWINDOW
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
            SelectDefaultICCProfile();
            ImgItem::UnloadDefaultICCProfile();
            browser_.ReloadCurrentItem();
            InvalidateScreen();
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
    case WM_MOUSEMOVE:
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
        PostQuitMessage(0);

        return 0;
    }

    return Window::HandleMessage(uMsg, wParam, lParam);
}

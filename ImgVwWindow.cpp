#include "ImgVwWindow.h"

ImgVwWindow* ImgVwWindow::Create(HINSTANCE hInst, std::vector<std::wstring> args)
{
	auto self = new ImgVwWindow(hInst, args);
	if (self != NULL)
	{
		self->backgroundbrush_ = CreateSolidBrush(RGB(0, 0, 0));
		self->manualcursor_ = TRUE;
		self->dontfillbackground_ = TRUE;
		if (self->WinCreateWindow(WS_EX_APPWINDOW,
			L"ImgVw",
			WS_POPUP,
			0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
			NULL, NULL))
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
	if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, 0, &nonclientmetrics, 0))
	{
		captionfont_ = CreateFontIndirectW(&nonclientmetrics.lfMessageFont);
	}

	QueryPerformanceFrequency(&frequency_);

	InitializeBrowser(path_);

	return FALSE;
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
	auto replacedobject = SelectObject(memorydc, item->displaybitmap());

	ExcludeClipRect(dc, item->offsetx(), item->offsety(), item->offsetx() + item->displaywidth(), item->offsety() + item->displayheight());
	if (FillRect(dc, &windowrectangle, backgroundbrush_) == 0)
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

	if (!mousemoved_)
	{
		MoveMouse();
		mousemoved_ = TRUE;
	}
}

void ImgVwWindow::MoveMouse()
{
	POINT p;
	if (!GetCursorPos(&p))
	{
		// TODO: handle error
	}

	auto x = p.x == 0 ? 1 : p.x - 1;
	auto y = p.y == 0 ? 1 : p.y - 1;

	INPUT input[1];
	input->type = INPUT_MOUSE;
	input->mi.dx = (LONG)(x * (65536.0f / GetSystemMetrics(SM_CXSCREEN)));
	input->mi.dy = (LONG)(y * (65536.0f / GetSystemMetrics(SM_CYSCREEN)));
	input->mi.mouseData = 0;
	input->mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
	input->mi.time = 0;
	input->mi.dwExtraInfo = 0;

	SendInput(1, input, sizeof(INPUT));

	input->type = INPUT_MOUSE;
	input->mi.dx = (LONG)(p.x * (65536.0f / GetSystemMetrics(SM_CXSCREEN)));
	input->mi.dy = (LONG)(p.y * (65536.0f / GetSystemMetrics(SM_CYSCREEN)));
	input->mi.mouseData = 0;
	input->mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
	input->mi.time = 0;
	input->mi.dwExtraInfo = 0;

	SendInput(1, input, sizeof(INPUT));
}

void ImgVwWindow::DisplayFileInformation(HDC dc, std::wstring filepath)
{
	RECT windowrectangle;
	GetWindowRect(hwnd_, &windowrectangle);
	FillRect(dc, &windowrectangle, backgroundbrush_);
	TextOut(dc, 0, 0, filepath.c_str(), filepath.size());
}

void ImgVwWindow::OnNCDestroy()
{
	browser_.StopBrowsing();

	DeleteObject(backgroundbrush_);
	DeleteObject(captionfont_);
}

BOOL CALLBACK ImgVwWindow::AboutDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
	{
		EndDialog(hwndDlg, 0);
		return TRUE;
	}

	return FALSE;
}

LRESULT ImgVwWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#if _DEBUG && LOG1
	kLogger.WriteLine(FormatWindowMessage(uMsg, wParam, lParam));
#endif

	switch (uMsg)
	{
	case WM_ACTIVATE:
		dlgcurrent_ = 0 == wParam ? NULL : hwnd_;
		return FALSE;
	case WM_CREATE:
		return OnCreate();
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDM_ABOUT:
			DialogBoxW(hinst_, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd_, (DLGPROC)AboutDialogProc);
			break;
		case IDR_NEXT:
			if (browser_.MoveToNext())
			{
				InvalidateRect(hwnd_, NULL, false);
			}

			break;
		case IDR_PREVIOUS:
			if (browser_.MoveToPrevious())
			{
				InvalidateRect(hwnd_, NULL, false);
			}

			break;
		case IDM_EXIT:
		case IDR_ESCAPE:
			if (!PostMessageW(hwnd_, WM_CLOSE, 0, 0))
			{
				// TODO: handle error
			}

			break;
		}

		return FALSE;
	case WM_SYSCOMMAND:
		if (LOWORD(wParam) == SC_CLOSE)
		{
			if (!PostMessageW(hwnd_, WM_CLOSE, 0, 0))
			{
				// TODO: handle error
			}
		}

		return FALSE;
	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT)
		{
			SetCursor(NULL);
			return TRUE;
		}

		break;
	case WM_ERASEBKGND:
		return TRUE;
	case WM_NCDESTROY:
		OnNCDestroy();
		break;
	case WM_SIZE:
	case WM_SETFOCUS:
		return FALSE;
	}

	return Window::HandleMessage(uMsg, wParam, lParam);
}

#if _DEBUG && FALSE
TimestampLogger ImgVwWindow::kLogger(L"C:\\Temp\\ImgVwWindow.log", TRUE);

std::wstring ImgVwWindow::FormatWindowMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	std::wstringstream wss;
	wss << L"message: " << std::setw(12) << std::setfill(L'0') << std::hex << uMsg << L"; ";
	wss << L"wParam: " << std::setw(12) << std::setfill(L'0') << std::hex << wParam << L"; ";
	wss << L"lParam: " << std::setw(18) << std::setfill(L'0') << std::hex << lParam << L"; ";
	return wss.str();
}
#endif

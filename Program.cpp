#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include "ImgVwWindow.h"
#include "TimestampLogger.h"
#include <string>
#include <vector>
#include <chrono>
#include <Windows.h>
#include <Ole2.h>
#include <CommCtrl.h>
#include <shellapi.h>

#if _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#if _MSC_VER
#pragma warning(suppress: 28251)
#endif

#if _DEBUG && LOG1
std::wstring FormatWindowMessage(MSG msg);
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	{
		int argscount;
		auto args = CommandLineToArgvW(GetCommandLineW(), &argscount);
		std::vector<std::wstring> argsvector;
		for (int i = 0; i < argscount; ++i)
		{
			argsvector.push_back(args[i]);
		}

		LocalFree(args);
#if _DEBUG && LOG1
		TimestampLogger logger(L"C:\\Temp\\Program.log", TRUE);
		logger.WriteLine(L"Start");
#endif

		if (SUCCEEDED(CoInitialize(NULL)))
		{
			InitCommonControls();

			Gdiplus::GdiplusStartupInput gdiplusStartupInput;
			ULONG_PTR gdiplusToken;
			Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

			auto imgvwwindow = ImgVwWindow::Create(hInstance, argsvector);
			if (imgvwwindow)
			{
				ShowWindow(imgvwwindow->hwnd(), nShowCmd);

				auto hacc = LoadAcceleratorsW(hInstance, MAKEINTRESOURCE(IDC_IMGVW));
				MSG msg;

				while (GetMessageW(&msg, NULL, 0, 0))
				{
#if _DEBUG && LOG1
					logger.WriteLine(FormatWindowMessage(msg));
#endif
					if (!TranslateAcceleratorW(imgvwwindow->hwnd(), hacc, &msg) ||
						imgvwwindow->dlgcurrent() == NULL ||
						!IsDialogMessageW(imgvwwindow->dlgcurrent(), &msg))
					{
						TranslateMessage(&msg);
						DispatchMessageW(&msg);
					}

					if (msg.message == WM_CLOSE)
					{
						break;
					}
				}

				DestroyAcceleratorTable(hacc);
			}

			Gdiplus::GdiplusShutdown(gdiplusToken);

			CoUninitialize();
		}

#if _DEBUG && LOG1
		logger.WriteLine(L"End");
#endif
	}

#if _DEBUG 
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}

#if _DEBUG && LOG1
std::wstring FormatWindowMessage(MSG msg)
{
	std::wstringstream wss;
	wss << L"hwnd: " << std::setw(8) << std::setfill(L'0') << std::hex << msg.hwnd << L"; ";
	wss << L"message: " << std::setw(8) << std::setfill(L'0') << std::hex << msg.message << L"; ";
	wss << L"wParam: " << std::setw(8) << std::setfill(L'0') << std::hex << msg.wParam << L"; ";
	wss << L"lParam: " << std::setw(8) << std::setfill(L'0') << std::hex << msg.lParam << L"; ";
	wss << L"time: " << msg.time << L"; ";
	wss << L"pt: " << msg.pt.x << L"," << msg.pt.y;
	return wss.str();
}
#endif
#pragma once

#include "ImgVwWindow.h"
#include "resource.h"
#include <Windows.h>
#include <shellapi.h>

#if _DEBUG
#include "DebugHelper.h"
#endif

class ImgVw
{
public:
    static INT Run(HINSTANCE hInstance, INT nShowCmd);
};

inline INT ImgVw::Run(HINSTANCE hInstance, INT nShowCmd)
{
    int argscount;
    auto args = CommandLineToArgvW(GetCommandLine(), &argscount);
    std::vector<std::wstring> argsvector;
    for (int i = 0; i < argscount; ++i)
    {
        argsvector.push_back(args[i]);
    }

    LocalFree(args);

#if _DEBUG
    TimestampLogger logger(L"C:\\Temp\\ImgVw_" + TimestampLogger::GetTimestampString(TRUE) + L".log", TRUE);
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

            auto hacc = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IMGVW));
            MSG msg;

            while (GetMessage(&msg, NULL, 0, 0))
            {
#if _DEBUG
                logger.WriteLine(DebugHelper::FormatWindowMessage(msg));
#endif
                if (!TranslateAccelerator(imgvwwindow->hwnd(), hacc, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }

            DestroyAcceleratorTable(hacc);
        }

        Gdiplus::GdiplusShutdown(gdiplusToken);

        CoUninitialize();
    }

    return 0;
}
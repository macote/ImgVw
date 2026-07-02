#pragma once

#include "ImgVwWindow.h"
#include "ProcessDpiAwareness.h"
#include "resource.h"
#include <Windows.h>
#include <shellapi.h>

class ImgVw
{
  public:
    static INT Run(HINSTANCE hInstance, INT nShowCmd);

  private:
    ImgVw() {}
};

inline INT ImgVw::Run(HINSTANCE hInstance, INT nShowCmd)
{
    ProcessDpiAwareness::EnableNativeMonitorPixels();

    INT argscount;
    const auto args = CommandLineToArgvW(GetCommandLine(), &argscount);
    std::vector<std::wstring> argsvector;
    for (INT i = 0; i < argscount; ++i)
    {
        argsvector.push_back(args[i]);
    }

    LocalFree(args);

    if (SUCCEEDED(CoInitialize(NULL)))
    {
        InitCommonControls();

        const Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        const auto imgvwwindow = ImgVwWindow::Create(hInstance, argsvector);
        if (imgvwwindow != nullptr)
        {
            ShowWindow(imgvwwindow->hwnd(), nShowCmd);

            const auto hacc = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_IMGVW));
            MSG msg;

            while (GetMessage(&msg, NULL, 0, 0))
            {
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

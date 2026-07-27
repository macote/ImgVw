#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ImgVwWindow.h"
#include "ImgSettings.h"
#include "ProcessDpiAwareness.h"
#include "resource.h"
#include <Windows.h>
#include <gdiplus.h>
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

    const auto& settings_result = ImgSettings::GetInstance().initialization_result();
    if (!settings_result.Succeeded())
    {
        std::wstringstream message;
        message << L"ImgVw could not initialize its temporary image directory (error 0x" << std::hex << std::setw(8)
                << std::setfill(L'0') << std::uppercase << static_cast<unsigned long>(settings_result.system_error)
                << L").";
        MessageBoxW(nullptr, message.str().c_str(), L"ImgVw", MB_OK | MB_ICONERROR);
        return 1;
    }

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
                if (!TranslateAccelerator(imgvwwindow->hwnd(), hacc, &msg) &&
                    !imgvwwindow->TranslateEmptyStateDialogMessage(&msg))
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

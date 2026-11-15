#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ImgVwWindow.h"
#include "ImgSettings.h"
#include "Localization.h"
#include "ProcessDpiAwareness.h"
#include "UiLanguageSettings.h"
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

    const auto language_preference = UiLanguageSettings::LoadLanguagePreference();
    const auto effective_language =
        ResolveUiLanguage(language_preference, UiLanguageSettings::DetectSystemUiLanguage());
    Localization::Initialize(hInstance, language_preference, effective_language);

    const auto& settings_result = ImgSettings::GetInstance().initialization_result();
    if (!settings_result.Succeeded())
    {
        std::wstringstream hex_error;
        hex_error << std::hex << std::setw(8) << std::setfill(L'0') << std::uppercase
                  << static_cast<unsigned long>(settings_result.system_error);
        std::wstring message_template;
        if (!Localization::TryLoadString(hInstance, IDS_ERROR_TEMP_DIR, effective_language, message_template))
        {
            message_template = L"ImgVw could not initialize its temporary image directory (error 0x{0}).";
        }
        std::wstring title;
        if (!Localization::TryLoadString(hInstance, IDS_APP_TITLE, effective_language, title))
        {
            title = L"ImgVw";
        }
        const auto message = FormatUiTemplate(message_template, {hex_error.str()});
        MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
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

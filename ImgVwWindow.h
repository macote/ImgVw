#pragma once

#include "Window.h"
#include "ImgBrowser.h"
#include "ImgItem.h"
#include <Windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <string>
#include <vector>

#define LOGIMGVWWINDOW 0
#if _DEBUG && LOGIMGVWWINDOW
#define LOGIMGVWWINDOWPATH L"C:\\Temp\\ImgVwWindow_"
#include "DebugHelper.h"
#endif

class ImgVwWindow final : public Window
{
public:
    static constexpr auto kClassName = L"ImgVwWindow";
    static const UINT kInitialSlideShowIntervalInMilliseconds = 4000;
    static const UINT kMinimumSlideShowIntervalInMilliseconds = 250;
    static const UINT kMaximumSlideShowIntervalInMilliseconds = 10000;
    static const UINT kSlideShowIntervalIncrementStepInMilliseconds = 250;
    static const UINT kMouseHideIntervalInMilliseconds = 1000;
public:
    ImgVwWindow(HINSTANCE hinst, const std::vector<std::wstring> args) : Window(hinst)
#if _DEBUG && LOGIMGVWWINDOW
        , logger_(TimestampLogger(LOGIMGVWWINDOWPATH + TimestampLogger::GetTimestampString(TRUE) + L".log", TRUE))
#endif
    {
        if (args.size() > 1)
        {
            path_ = args[1];
        }
    }
    ImgVwWindow(const ImgVwWindow&) = delete;
    ImgVwWindow& operator=(const ImgVwWindow&) = delete;
    LPCWSTR ClassName() const { return kClassName; }
    static ImgVwWindow* Create(HINSTANCE hInst, const std::vector<std::wstring> args);
private:
    void PaintContent(PAINTSTRUCT* pps);
    void DeleteCurrentItem(BOOL allowundo);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCreate();
    void InitializeBrowser(const std::wstring& path);
    void PerformAction();
    void BrowseNext();
    void BrowsePrevious();
    void BrowseFirst();
    void BrowseLast();
    void HandleMouseWheel(WORD distance);
    void ToggleSlideShow(BOOL slideshowrandom);
    void StartSlideShow();
    void StopSlideShow();
    void RestartSlideShowTimer();
    void IncreaseSlideShowSpeed();
    void DecreaseSlideShowSpeed();
    void HandleSlideShow();
    void InvalidateScreen();
    BOOL DisplayImage(HDC dc, const ImgItem* item);
    void DisplayFileInformation(HDC dc, const std::wstring& filepath);
    void CloseWindow();
    BOOL HandleMouseMove(WPARAM wParam, LPARAM lParam);
    void HandleHideMouseCursor();
    void OnNCDestroy();
    static BOOL CALLBACK AboutDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);
private:
    ImgBrowser browser_;
    std::wstring path_;
    WORD activeparam_{};
    HFONT captionfont_{ nullptr };
    HCURSOR arrowcursor_{ nullptr };
    LARGE_INTEGER qpcfrequency_{};
    BOOL slideshowrunning_{};
    BOOL slideshowrandom_{};
    UINT slideshowinterval_{ kInitialSlideShowIntervalInMilliseconds };
    POINTS mousemovelastpoints_{};
    LARGE_INTEGER mousemovelastcounter_{};
    BOOL mousehidetimerstarted_{ FALSE };
#if _DEBUG && LOGIMGVWWINDOW
    TimestampLogger logger_;
#endif
};

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

class ImgVwWindow : public Window
{
public:
    static const INT kInitialSlideShowIntervalInMilliseconds = 4000;
    static const INT kMinimumSlideShowIntervalInMilliseconds = 500;
    static const INT kMaximumSlideShowIntervalInMilliseconds = 10000;
    static const INT kSlideShowIntervalIncrementStepInMilliseconds = 500;
public:
    ImgVwWindow(HINSTANCE hinst, std::vector<std::wstring> args) : Window(hinst)
#if _DEBUG && LOGIMGVWWINDOW
        , logger_(TimestampLogger(LOGIMGVWWINDOWPATH + TimestampLogger::GetTimestampString(TRUE) + L".log", TRUE))
#endif
    {
        if (args.size() > 1)
        {
            path_ = args[1];
        }
    }
    LPCWSTR ClassName() { return L"ImgVwWindow"; }
    static ImgVwWindow* Create(HINSTANCE hInst, std::vector<std::wstring> args);
private:
    void PaintContent(PAINTSTRUCT* pps);
    void DeleteCurrentItem(BOOL allowundo);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCreate();
    void InitializeBrowser(std::wstring path);
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
    BOOL DisplayImage(HDC dc, ImgItem* item);
    void DisplayFileInformation(HDC dc, std::wstring filepath);
    void CloseWindow();
    void OnNCDestroy();
    static BOOL CALLBACK AboutDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam);
private:
    ImgBrowser browser_;
    std::wstring path_;
    WORD activeparam_{};
    HFONT captionfont_{ nullptr };
    LARGE_INTEGER frequency_{};
    BOOL slideshowrunning_{};
    BOOL slideshowrandom_{};
    UINT slideshowinterval_{ kInitialSlideShowIntervalInMilliseconds };
#if _DEBUG && LOGIMGVWWINDOW
    TimestampLogger logger_;
#endif
};

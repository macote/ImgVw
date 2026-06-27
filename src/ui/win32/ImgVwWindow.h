#pragma once

#include "FileOperations.h"
#include "ImgRenderer.h"
#include "Window.h"
#include "ImgBrowser.h"
#include "ImgItem.h"
#include "ImgSettings.h"
#include <Windows.h>
#include <Windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <string>
#include <vector>

class ImgVwWindow final : public Window
{
  public:
    static constexpr auto kClassName = L"ImgVwWindow";
    static constexpr auto kAppDataPath = L"A611FF5773EC43EC\\ImgVw";
    static const UINT kInitialSlideShowIntervalInMilliseconds = 1750;
    static const UINT kMinimumSlideShowIntervalInMilliseconds = 125;
    static const UINT kMaximumSlideShowIntervalInMilliseconds = 10000;
    static const UINT kSlideShowIntervalIncrementStepInMilliseconds = 125;
    static const UINT kMouseHideIntervalInMilliseconds = 1000;
    static const UINT kBrowserChangedMessage = WM_APP + 1;

  public:
    ImgVwWindow(HINSTANCE hinst, const std::vector<std::wstring> args)
        : Window(hinst)
    {
        if (args.size() > 1)
        {
            path_ = args[1];
        }
    }
    ImgVwWindow(const ImgVwWindow&) = delete;
    ImgVwWindow& operator=(const ImgVwWindow&) = delete;
    LPCWSTR ClassName() const
    {
        return kClassName;
    }
    static ImgVwWindow* Create(HINSTANCE hInst, const std::vector<std::wstring>& args);

  private:
    ImgBrowser browser_;
    FileOperations file_operations_;
    ImgRenderer image_renderer_;
    std::wstring path_;
    WORD activeparam_{};
    HFONT captionfont_{nullptr};
    HCURSOR arrowcursor_{nullptr};
    LARGE_INTEGER qpcfrequency_{};
    BOOL slideshowrunning_{};
    BOOL slideshowrandom_{};
    BOOL slideshowwaitingforimage_{};
    UINT slideshowinterval_{kInitialSlideShowIntervalInMilliseconds};
    POINTS mousemovelastpoints_{};
    LARGE_INTEGER mousemovelastcounter_{};
    BOOL mousehidetimerstarted_{FALSE};
    INT clientwidth_{};
    INT clientheight_{};
    HMONITOR currentmonitor_{nullptr};
    BOOL draggingwindow_{FALSE};
    POINT dragstartpoint_{};
    RECT dragstartwindowrect_{};
  private:
    void InitializeBrowser(const std::wstring& path);
    BOOL UpdateClientSize(INT width, INT height);
    void HandleSize(WPARAM wParam, LPARAM lParam);
    void InitializeMonitorState();
    void HandleWindowPosChanged();
    BOOL ApplyMonitorBounds(HMONITOR monitor);
    BOOL HasMultipleMonitors() const;
    BOOL BeginWindowDrag(LPARAM lParam);
    BOOL UpdateWindowDrag(WPARAM wParam, LPARAM lParam);
    void EndWindowDrag();
    void PerformAction();
    void BrowseNext();
    void BrowsePrevious();
    void BrowseFirst();
    void BrowseLast();
    void BrowseSubFolders();
    void HandleMouseWheel(WORD distance);
    void ToggleSlideShow(BOOL slideshowrandom);
    void StartSlideShow();
    void StopSlideShow();
    void RestartSlideShowTimer();
    void IncreaseSlideShowSpeed();
    void DecreaseSlideShowSpeed();
    void HandleSlideShow();
    void DisplayCurrentSlideWhenReady();
    void HandleBrowserChanged();
    BOOL SelectDefaultICCProfile();
    void UseBuiltInICCProfile();
    void HandleContextMenu(LPARAM lParam);
    void InvalidateScreen();
    bool DisplayImage(HDC dc, const ImgItem* item);
    void DisplayFileInformation(HDC dc, const std::wstring& filepath);
    void PaintContent(PAINTSTRUCT* pps);
    void DeleteCurrentItem(BOOL allowundo);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnCreate();
    void CloseWindow();
    BOOL HandleMouseMove(WPARAM wParam, LPARAM lParam);
    void HandleHideMouseCursor();
    void OnNCDestroy();
    static BOOL CALLBACK AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

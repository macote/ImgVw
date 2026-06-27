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
#include <cstddef>
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
    ImgVwWindow(HINSTANCE hinst, const std::wstring& path, ImgVwWindow* owner, BOOL primary)
        : Window(hinst), path_(path), owner_(owner), primarywindow_(primary)
    {
    }
    ImgVwWindow(const ImgVwWindow&) = delete;
    ImgVwWindow& operator=(const ImgVwWindow&) = delete;
    LPCWSTR ClassName() const
    {
        return kClassName;
    }
    static ImgVwWindow* Create(HINSTANCE hInst, const std::vector<std::wstring>& args);

  private:
    struct MonitorCreateContext;

    ImgBrowser browser_;
    FileOperations file_operations_;
    ImgRenderer image_renderer_;
    std::wstring path_;
    std::wstring displayslidepath_;
    std::wstring paintedslidepath_;
    WORD activeparam_{};
    HFONT captionfont_{nullptr};
    HCURSOR arrowcursor_{nullptr};
    LARGE_INTEGER qpcfrequency_{};
    BOOL slideshowrunning_{};
    BOOL slideshowrandom_{};
    BOOL slideshowwaitingforimage_{};
    BOOL slideshowneedsinitialadvance_{};
    UINT slideshowinterval_{kInitialSlideShowIntervalInMilliseconds};
    POINTS mousemovelastpoints_{};
    LARGE_INTEGER mousemovelastcounter_{};
    BOOL mousehidetimerstarted_{FALSE};
    INT clientwidth_{};
    INT clientheight_{};
    BOOL browsesubfolders_{FALSE};
    HMONITOR currentmonitor_{nullptr};
    BOOL draggingwindow_{FALSE};
    POINT dragstartpoint_{};
    RECT dragstartwindowrect_{};
    ImgVwWindow* owner_{nullptr};
    BOOL primarywindow_{TRUE};
    BOOL multimonitorslideshowrunning_{FALSE};
    std::size_t multimonitorslideshowindex_{};
    std::vector<ImgVwWindow*> slideshowwindows_;
  private:
    static ImgVwWindow* CreateOnMonitor(HINSTANCE hInst, const std::wstring& path, HMONITOR monitor,
                                        ImgVwWindow* owner);
    static BOOL CALLBACK CreateSlideShowWindowForMonitor(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM param);
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
    void FinishWindowDrag();
    void PerformAction();
    void BrowseNext();
    void BrowsePrevious();
    void BrowseFirst();
    void BrowseLast();
    void BrowseSubFolders();
    void EnableBrowseSubFolders();
    void HandleMouseWheel(WORD distance);
    void HandleMultiMonitorMouseWheel(ImgVwWindow* target);
    void ToggleSlideShow(BOOL slideshowrandom);
    void StartSlideShow();
    void StopSlideShow();
    void ToggleMultiMonitorRandomSlideShow();
    void StartMultiMonitorRandomSlideShow();
    void StopMultiMonitorRandomSlideShow();
    void RestartMultiMonitorSlideShowTimer();
    void HandleMultiMonitorSlideShow();
    ImgVwWindow* MultiMonitorSlideShowWindowAt(std::size_t index);
    std::size_t MultiMonitorSlideShowWindowCount() const;
    void DestroySlideShowWindows();
    void OnSlideShowWindowDestroyed(ImgVwWindow* window);
    void CloseOwnedWindows();
    ImgVwWindow* CommandTarget();
    void RestartSlideShowTimer();
    void IncreaseSlideShowSpeed();
    void DecreaseSlideShowSpeed();
    void HandleSlideShow();
    BOOL AdvanceRandomSlide(BOOL restarttimer);
    BOOL AdvanceSharedRandomSlide(ImgVwWindow* target);
    void RestoreSharedRandomOwnerDisplayCursor();
    BOOL IsSlidePathVisible(const std::wstring& filepath);
    BOOL DisplaySlidePath(const std::wstring& filepath);
    void DisplayCurrentSlideWhenReady();
    void DisplayCurrentSlideWithoutTimer();
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

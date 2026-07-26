#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "FileOperations.h"
#include "BrowseWindowState.h"
#include "EmptyStateView.h"
#include "DisplayPresenter.h"
#include "DisplaySession.h"
#include "CursorController.h"
#include "InfoOverlay.h"
#include "InfoOverlayStatsBuilder.h"
#include "InfoOverlayVisibility.h"
#include "MonitorPlacement.h"
#include "MultiMonitorSlideShowCoordinator.h"
#include "SlideShowStateMachine.h"
#include "WindowGeometry.h"
#include "WindowDragController.h"
#include "Window.h"
#include "ImgBrowser.h"
#include "ImgItem.h"
#include "ImgSettings.h"
#include "PathPicker.h"
#include <Windows.h>
#include <shellapi.h>
#include <cstddef>
#include <memory>
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
    static const UINT kBrowserCompletionRetryTimer = 4;
    static const UINT kBrowserCompletionRetryIntervalInMilliseconds = 10;
    static const UINT kLoaderStatsOverlayTimer = 2;
    static const UINT kLoaderStatsOverlayIntervalInMilliseconds = 250;
    static const UINT kLoadingProgressOverlayTimer = 3;
    static const UINT kLoadingProgressOverlayIntervalInMilliseconds = 100;
    static const DWORD kLoadingProgressOverlayDebounceInMilliseconds = 666;

  public:
    ImgVwWindow(HINSTANCE hinst, const std::vector<std::wstring> args)
        : Window(hinst), browse_state_(args.size() > 1 ? args[1] : std::wstring())
    {
    }
    ImgVwWindow(HINSTANCE hinst, const std::wstring& path, ImgVwWindow* owner, BOOL primary)
        : Window(hinst), browse_state_(path), owner_(owner), primarywindow_(primary)
    {
    }
    ImgVwWindow(const ImgVwWindow&) = delete;
    ImgVwWindow& operator=(const ImgVwWindow&) = delete;
    LPCWSTR ClassName() const
    {
        return kClassName;
    }
    static ImgVwWindow* Create(HINSTANCE hInst, const std::vector<std::wstring>& args);
    BOOL TranslateEmptyStateDialogMessage(MSG* message) const;

  private:
    struct MonitorCreateContext;
    ImgBrowser browser_;
    BrowseWindowState browse_state_;
    PathPicker path_picker_;
    FileOperations file_operations_;
    DisplayPresenter display_presenter_;
    LARGE_INTEGER qpcfrequency_{};
    SlideShowStateMachine slideshow_{kInitialSlideShowIntervalInMilliseconds, kMinimumSlideShowIntervalInMilliseconds,
                                     kMaximumSlideShowIntervalInMilliseconds,
                                     kSlideShowIntervalIncrementStepInMilliseconds};
    CursorController cursor_controller_;
    INT clientwidth_{};
    INT clientheight_{};
    EmptyStateView empty_state_view_;
    MonitorPlacement monitor_placement_;
    WindowDragController window_drag_controller_;
    ImgVwWindow* owner_{nullptr};
    BOOL primarywindow_{TRUE};
    MultiMonitorSlideShowCoordinator multi_monitor_slideshow_;
    DisplaySession display_session_;

    InfoOverlayVisibility info_overlay_visibility_;
    BOOL systemlighttheme_{FALSE};
    InfoOverlay info_overlay_;

  private:
    static ImgVwWindow* CreateOnMonitor(HINSTANCE hInst, const std::wstring& path, HMONITOR monitor,
                                        ImgVwWindow* owner);
    static BOOL CALLBACK CreateSlideShowWindowForMonitor(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM param);
    BOOL InitializeBrowser(const std::wstring& path, BOOL clearloadcontext = FALSE);
    BOOL OpenPath(const std::wstring& path);
    void OpenImage();
    void OpenFolder();
    void ActivateEmptyStateButton();
    void SelectPath(const PathPickerResult& result);
    void HandleDroppedFiles(HDROP drop);
    void BrowseEmptyStateSubFolders();
    void ShowEmptyState(const std::wstring& message, BOOL show_search_subfolders);
    void ShowSearchingSubfoldersState();
    void HideEmptyState();
    BOOL IsEmptyStateVisible() const;
    BOOL IsSearchingSubfolders() const;
    BOOL HasImages();
    BOOL UpdateClientSize(INT width, INT height);
    void HandleSize(WPARAM wParam, LPARAM lParam);
    void HandleDpiChanged(LPARAM lParam);
    void InitializeMonitorState();
    void UpdateSystemTheme();
    void HandleWindowPosChanged();
    BOOL ApplyMonitorBounds(HMONITOR monitor);
    BOOL HasMultipleMonitors() const;
    BOOL BeginWindowDrag(LPARAM lParam);
    BOOL UpdateWindowDrag(WPARAM wParam, LPARAM lParam);
    void EndWindowDrag();
    void FinishWindowDrag();
    void ToggleFilenameOverlay();
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
    void ToggleMultiMonitorSlideShow(BOOL slideshowrandom);
    void StartMultiMonitorSlideShow(BOOL slideshowrandom);
    void StopMultiMonitorSlideShow();
    void RestartMultiMonitorSlideShowTimer();
    void HandleMultiMonitorSlideShow();
    void RefreshMultiMonitorPreloadContexts();
    std::size_t PreloadMultiMonitorSlideShowContexts();
    static MultiMonitorWindowId WindowId(HWND window);
    static HWND WindowHandle(MultiMonitorWindowId window);
    static ImgVwWindow* ResolveSlideShowWindow(MultiMonitorWindowId window);
    ImgVwWindow* MultiMonitorSlideShowWindowAt(std::size_t index);
    std::size_t MultiMonitorSlideShowWindowCount() const;
    void DestroySlideShowWindows();
    void OnSlideShowWindowDestroyed(ImgVwWindow* window);
    std::shared_ptr<ImgBrowserLoadContext> FindTargetLoadContext(INT width, INT height) const;
    void RememberTargetLoadContext(INT width, INT height, const std::shared_ptr<ImgBrowserLoadContext>& context);
    void CloseOwnedWindows();
    ImgVwWindow* CommandTarget();
    void RestartSlideShowTimer();
    void IncreaseSlideShowSpeed();
    void DecreaseSlideShowSpeed();
    void HandleSlideShow();
    BOOL AdvanceRandomSlide(BOOL restarttimer);
    BOOL AdvanceSharedSlide(ImgVwWindow* target);
    BOOL AdvanceSharedSequentialSlide(ImgVwWindow* target);
    BOOL AdvanceSharedRandomSlide(ImgVwWindow* target);
    void RestoreSharedOwnerDisplayCursor();
    BOOL DisplaySlidePath(const std::wstring& filepath);
    void DisplayCurrentSlideWhenReady();
    void DisplayCurrentSlideWithoutTimer();
    void HandleStartupExitConditions();
    void HandleBrowserChanged();
    BOOL SelectDefaultICCProfile();
    void UseBuiltInICCProfile();
    void UpdateContextMenuForMonitorCount(HMENU menu) const;
    void UpdateContextMenuForImageAvailability(HMENU menu);
    void HandleContextMenu(LPARAM lParam);
    void InvalidateScreen();
    bool DisplayFileInformation(HDC dc, const RECT& paintrect, const ImgItem* item, const std::wstring& filepath);
    bool DisplayLoadingProgress(HDC dc, const RECT& paintrect, const ImgItem* item, const std::wstring& filepath);
    void UpdateLoadingProgressOverlayTimer();
    BOOL IsLoaderStatsOverlayKeyDown() const;
    BOOL IsFilenameOverlayVisible() const;
    BOOL IsInfoOverlayVisible() const;
    void UpdateLoaderStatsOverlayVisibility();
    void UpdateLoaderStatsOverlayVisibilityForWindow();
    void UpdateInfoOverlayForWindow();
    void ClearInfoOverlay();
    std::wstring BuildLoaderStatsOverlayText();
    UINT GetWindowDpi() const;
    void ResetLoaderStatsOverlayLayout();
    void RefreshLoaderStatsOverlay();
    void DrawLoaderStatsOverlay(HDC dc, const ImgItem* item);
    void PaintContent(PAINTSTRUCT* pps);
    void DeleteCurrentItem(BOOL allowundo);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleCommand(UINT command, UINT notification, LPARAM lparam);
    LRESULT HandleTimer(UINT_PTR timer, LPARAM callback);
    LRESULT HandleMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnCreate();
    void CloseWindow();
    BOOL HandleMouseMove(WPARAM wParam, LPARAM lParam);
    void HandleHideMouseCursor();
    void ApplyCursorVisibility(CursorVisibilityAction action);
    void ApplyCursorCapture(bool captured);
    void OnNCDestroy();
    static BOOL CALLBACK AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

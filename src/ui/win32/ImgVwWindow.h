#pragma once

#include "FileOperations.h"
#include "ImgRenderer.h"
#include "Window.h"
#include "ImgBrowser.h"
#include "ImgItem.h"
#include "ImgSettings.h"
#include "PathPicker.h"
#include <Windows.h>
#include <Windowsx.h>
#include <Gdiplus.h>
#include <commctrl.h>
#include <objidl.h>
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
    static const UINT kLoaderStatsOverlayTimer = 2;
    static const UINT kLoaderStatsOverlayIntervalInMilliseconds = 250;
    static const UINT kLoadingProgressOverlayTimer = 3;
    static const UINT kLoadingProgressOverlayIntervalInMilliseconds = 100;
    static const DWORD kLoadingProgressOverlayDebounceInMilliseconds = 666;

  public:
    ImgVwWindow(HINSTANCE hinst, const std::vector<std::wstring> args) : Window(hinst)
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
    BOOL TranslateEmptyStateDialogMessage(MSG* message) const;

  private:
    struct MonitorCreateContext;
    struct EmptyStateLayout
    {
        INT panel_width{};
        INT panel_height{};
        INT logo_width{};
        INT logo_height{};
        INT buttons_top{};
    };
    enum class BrowseUiState
    {
        Collecting,
        Viewing,
        Empty,
        NoImages,
        SearchingSubfolders,
    };

    ImgBrowser browser_;
    PathPicker path_picker_;
    FileOperations file_operations_;
    ImgRenderer image_renderer_;
    std::wstring path_;
    std::wstring displayslidepath_;
    std::wstring paintedslidepath_;
    WORD activeparam_{};
    HFONT captionfont_{nullptr};
    std::unique_ptr<Gdiplus::Image> emptystatelogo_;
    IStream* emptystatelogostream_{nullptr};
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
    BOOL browserinitialized_{FALSE};
    BrowseUiState browseuistate_{BrowseUiState::Collecting};
    std::wstring emptystatemessage_;
    HWND openimagebutton_{nullptr};
    HWND openfolderbutton_{nullptr};
    HWND searchsubfoldersbutton_{nullptr};
    HWND exitbutton_{nullptr};
    HMONITOR currentmonitor_{nullptr};
    BOOL draggingwindow_{FALSE};
    POINT dragstartpoint_{};
    RECT dragstartwindowrect_{};
    ImgVwWindow* owner_{nullptr};
    BOOL primarywindow_{TRUE};
    BOOL multimonitorslideshowrunning_{FALSE};
    std::size_t multimonitorslideshowindex_{};
    std::wstring multimonitorslideshowcursorpath_;
    std::vector<ImgVwWindow*> slideshowwindows_;
    BOOL firstimagepaint_{TRUE};

    BOOL filenameoverlayenabled_{FALSE};
    BOOL loaderstatsoverlayvisible_{FALSE};
    BOOL systemlighttheme_{FALSE};
    std::wstring loaderstatsoverlaytext_;
    RECT loaderstatsoverlayrect_{};
    HFONT loaderstatsoverlayfont_{nullptr};
    UINT loaderstatsoverlayfontdpi_{};
    INT lastloadingprogresspercent_{-2};
    DWORD loadingprogresswaitstarttick_{};
    BOOL loadingprogressoverlayvisible_{FALSE};
    std::wstring loadingprogresspath_;

  private:
    static ImgVwWindow* CreateOnMonitor(HINSTANCE hInst, const std::wstring& path, HMONITOR monitor,
                                        ImgVwWindow* owner);
    static BOOL CALLBACK CreateSlideShowWindowForMonitor(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM param);
    BOOL InitializeBrowser(const std::wstring& path);
    BOOL OpenPath(const std::wstring& path);
    void OpenImage();
    void OpenFolder();
    void RestoreEmptyStateButtonFocus(HWND button);
    void SelectPath(const PathPickerResult& result);
    void HandleDroppedFiles(HDROP drop);
    void BrowseEmptyStateSubFolders();
    void LoadEmptyStateLogo();
    void CreateEmptyStateControls();
    void ShowEmptyState(const std::wstring& message, BOOL show_search_subfolders);
    void ShowSearchingSubfoldersState();
    void HideEmptyState();
    BOOL IsEmptyStateVisible() const;
    BOOL IsSearchingSubfolders() const;
    EmptyStateLayout CalculateEmptyStateLayout(const RECT& client_rect) const;
    void UpdateEmptyStateLayout();
    void PaintEmptyState(PAINTSTRUCT* pps);
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
    void HandleContextMenu(LPARAM lParam);
    void InvalidateScreen();
    bool DisplayImage(HDC dc, const ImgItem* item);
    bool DisplayFileInformation(HDC dc, const RECT& paintrect, const ImgItem* item, const std::wstring& filepath);
    bool DisplayLoadingProgress(HDC dc, const RECT& paintrect, const ImgItem* item, const std::wstring& filepath);
    std::wstring BuildItemInfoOverlayText(const ImgItem* item, const std::wstring& filepath) const;
    BOOL IsLoadingProgressOverlayVisible(const ImgItem* item) const;
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
    INT ScaleForWindowDpi(INT value) const;
    HFONT GetLoaderStatsOverlayFont();
    void ResetLoaderStatsOverlayLayout();
    RECT CalculateLoaderStatsOverlayRect(HDC dc, const std::wstring& text) const;
    void RefreshLoaderStatsOverlay();
    void DrawTextOverlay(HDC dc, const RECT& overlayrect, const std::wstring& text, const ImgItem* item,
                         UINT textformat = DT_LEFT | DT_NOPREFIX, COLORREF fallbackbackground = RGB(0, 0, 0),
                         BOOL vertically_center_text = FALSE);
    void DrawEmptyStateButton(const DRAWITEMSTRUCT* drawitem);
    void DrawLoaderStatsOverlay(HDC dc, const ImgItem* item);
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

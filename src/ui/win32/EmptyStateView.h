#pragma once

#include "EmptyStateLayout.h"
#include "ComPtr.h"
#include "GdiObject.h"
#include "resource.h"

#include <Windows.h>

#include <propidl.h>
#include <Gdiplus.h>
#include <objidl.h>

#include <memory>
#include <string>

class EmptyStateView
{
  public:
    enum class State
    {
        Hidden,
        Empty,
        NoImages,
        SearchingSubfolders,
    };

    EmptyStateView() = default;
    ~EmptyStateView();
    EmptyStateView(const EmptyStateView&) = delete;
    EmptyStateView& operator=(const EmptyStateView&) = delete;

    void Initialize(HWND parent, HINSTANCE instance);
    void Show(const std::wstring& message, bool no_images, bool show_search_subfolders);
    void ShowSearchingSubfolders();
    void Hide();
    bool IsVisible() const;
    bool IsSearchingSubfolders() const;
    bool IsNoImages() const;
    const std::wstring& message() const;
    void RestoreFocus(UINT command) const;
    void ActivateFocusedButton() const;
    void UpdateLayout(UINT dpi) const;
    void Paint(HDC dc, UINT dpi, bool light_theme);
    void DrawButton(const DRAWITEMSTRUCT* draw_item, UINT dpi, bool light_theme);
    BOOL TranslateDialogMessage(MSG* message) const;

  private:
    static HMENU CommandMenu(UINT command);
    HWND ButtonForCommand(UINT command) const;
    bool IsButton(HWND window) const;
    void CreateCaptionFont();
    void LoadLogo();
    void DestroyControls();
    void DrawPanelText(HDC dc, const RECT& panel_rect, const std::wstring& text, UINT text_format, UINT dpi,
                       bool light_theme, bool vertically_center_text) const;
    HFONT GetTextFont(UINT dpi);

    State state_{State::Hidden};
    std::wstring message_;
    HWND parent_{nullptr};
    HINSTANCE instance_{nullptr};
    HWND open_image_button_{nullptr};
    HWND open_folder_button_{nullptr};
    HWND search_subfolders_button_{nullptr};
    HWND exit_button_{nullptr};
    GdiObject<HFONT> caption_font_;
    GdiObject<HFONT> text_font_;
    UINT text_font_dpi_{};
    std::unique_ptr<Gdiplus::Image> logo_;
    ComPtr<IStream> logo_stream_;
};

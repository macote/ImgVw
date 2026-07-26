#include "EmptyStateView.h"
#include "CompatibleDeviceContext.h"
#include "GlobalMemory.h"
#include "SelectedGdiObject.h"

#include <algorithm>
#include <cstring>

namespace
{
struct Colors
{
    COLORREF background;
    COLORREF panel;
    COLORREF border;
    COLORREF text;
};

Colors GetColors(bool light_theme)
{
    return light_theme ? Colors{RGB(245, 245, 245), RGB(255, 255, 255), RGB(140, 140, 140), RGB(50, 50, 50)}
                       : Colors{RGB(0, 0, 0), RGB(0, 0, 0), RGB(90, 90, 90), RGB(180, 180, 180)};
}
} // namespace

EmptyStateView::~EmptyStateView()
{
    DestroyControls();
    logo_.reset();
}

void EmptyStateView::Initialize(HWND parent, HINSTANCE instance)
{
    if (parent == nullptr || open_image_button_ != nullptr)
    {
        return;
    }

    parent_ = parent;
    instance_ = instance;
    CreateCaptionFont();
    LoadLogo();

    const auto button_style = WS_CHILD | WS_TABSTOP | BS_OWNERDRAW;
    open_image_button_ = CreateWindow(L"BUTTON", L"Open &image...", button_style, 0, 0, 0, 0, parent,
                                      CommandMenu(IDM_OPEN_IMAGE), instance, nullptr);
    open_folder_button_ = CreateWindow(L"BUTTON", L"Open &folder...", button_style, 0, 0, 0, 0, parent,
                                       CommandMenu(IDM_OPEN_FOLDER), instance, nullptr);
    search_subfolders_button_ = CreateWindow(L"BUTTON", L"&Search subfolders", button_style, 0, 0, 0, 0, parent,
                                             CommandMenu(IDM_SEARCH_SUBFOLDERS), instance, nullptr);
    exit_button_ =
        CreateWindow(L"BUTTON", L"E&xit", button_style, 0, 0, 0, 0, parent, CommandMenu(IDM_EXIT), instance, nullptr);

    for (const auto button : {open_image_button_, open_folder_button_, search_subfolders_button_, exit_button_})
    {
        if (button != nullptr && caption_font_.valid())
        {
            SendMessage(button, WM_SETFONT, reinterpret_cast<WPARAM>(caption_font_.get()), TRUE);
        }
    }
}

void EmptyStateView::Show(const std::wstring& message, bool no_images, bool show_search_subfolders)
{
    state_ = no_images ? State::NoImages : State::Empty;
    message_ = message;
    ShowWindow(open_image_button_, SW_SHOW);
    ShowWindow(open_folder_button_, SW_SHOW);
    ShowWindow(search_subfolders_button_, show_search_subfolders ? SW_SHOW : SW_HIDE);
    ShowWindow(exit_button_, SW_SHOW);
    SetFocus(show_search_subfolders && search_subfolders_button_ != nullptr ? search_subfolders_button_
                                                                            : open_image_button_);
}

void EmptyStateView::ShowSearchingSubfolders()
{
    state_ = State::SearchingSubfolders;
    message_ = L"Searching subfolders for supported images...";
    ShowWindow(open_image_button_, SW_HIDE);
    ShowWindow(open_folder_button_, SW_HIDE);
    ShowWindow(search_subfolders_button_, SW_HIDE);
    ShowWindow(exit_button_, SW_SHOW);
    SetFocus(exit_button_);
}

void EmptyStateView::Hide()
{
    state_ = State::Hidden;
    message_.clear();
    for (const auto button : {open_image_button_, open_folder_button_, search_subfolders_button_, exit_button_})
    {
        ShowWindow(button, SW_HIDE);
    }
}

bool EmptyStateView::IsVisible() const
{
    return state_ != State::Hidden;
}

bool EmptyStateView::IsSearchingSubfolders() const
{
    return state_ == State::SearchingSubfolders;
}

bool EmptyStateView::IsNoImages() const
{
    return state_ == State::NoImages;
}

const std::wstring& EmptyStateView::message() const
{
    return message_;
}

void EmptyStateView::RestoreFocus(UINT command) const
{
    const auto button = ButtonForCommand(command);
    if (!IsVisible() || button == nullptr || !IsWindowVisible(button))
    {
        return;
    }

    SetFocus(button);
    if (GetFocus() == button)
    {
        InvalidateRect(button, nullptr, FALSE);
        UpdateWindow(button);
    }
}

void EmptyStateView::ActivateFocusedButton() const
{
    if (!IsVisible())
    {
        return;
    }

    const auto focused_window = GetFocus();
    const auto focused_button = IsButton(focused_window) ? focused_window : open_image_button_;
    if (focused_button != nullptr)
    {
        SendMessage(focused_button, BM_CLICK, 0, 0);
    }
}

void EmptyStateView::UpdateLayout(UINT dpi) const
{
    if (parent_ == nullptr || open_image_button_ == nullptr || open_folder_button_ == nullptr)
    {
        return;
    }

    RECT client_rect{};
    if (!GetClientRect(parent_, &client_rect))
    {
        return;
    }

    const EmptyStateLayout::Input layout_input{client_rect.right,
                                               client_rect.bottom,
                                               dpi,
                                               IsNoImages(),
                                               IsSearchingSubfolders(),
                                               logo_ != nullptr ? static_cast<INT>(logo_->GetWidth()) : 0,
                                               logo_ != nullptr ? static_cast<INT>(logo_->GetHeight()) : 0};
    const auto layout = EmptyStateLayout::Calculate(layout_input);
    const auto button_width = WindowGeometry::ScaleForDpi(150, dpi);
    const auto button_height = WindowGeometry::ScaleForDpi(28, dpi);
    const auto gap = WindowGeometry::ScaleForDpi(12, dpi);
    const auto buttons_width = button_width * 2 + gap;
    const auto left = (client_rect.right - buttons_width) / 2;
    const auto top = layout.buttons_top;
    if (IsSearchingSubfolders())
    {
        MoveWindow(exit_button_, (client_rect.right - button_width) / 2, top, button_width, button_height, TRUE);
        return;
    }

    MoveWindow(open_image_button_, left, top, button_width, button_height, TRUE);
    MoveWindow(open_folder_button_, left + button_width + gap, top, button_width, button_height, TRUE);
    const auto secondary_top = top + button_height + gap;
    const auto search_visible = search_subfolders_button_ != nullptr && IsWindowVisible(search_subfolders_button_);
    MoveWindow(search_subfolders_button_, search_visible ? left : (client_rect.right - button_width) / 2, secondary_top,
               button_width, button_height, TRUE);
    MoveWindow(exit_button_, search_visible ? left + button_width + gap : (client_rect.right - button_width) / 2,
               secondary_top, button_width, button_height, TRUE);
}

void EmptyStateView::Paint(HDC dc, UINT dpi, bool light_theme)
{
    if (dc == nullptr || parent_ == nullptr)
    {
        return;
    }

    RECT client_rect{};
    if (!GetClientRect(parent_, &client_rect))
    {
        return;
    }

    const auto colors = GetColors(light_theme);
    GdiObject<HBRUSH> background(CreateSolidBrush(colors.background));
    if (background.valid())
    {
        FillRect(dc, &client_rect, background.get());
    }

    const EmptyStateLayout::Input layout_input{client_rect.right,
                                               client_rect.bottom,
                                               dpi,
                                               IsNoImages(),
                                               IsSearchingSubfolders(),
                                               logo_ != nullptr ? static_cast<INT>(logo_->GetWidth()) : 0,
                                               logo_ != nullptr ? static_cast<INT>(logo_->GetHeight()) : 0};
    const auto layout = EmptyStateLayout::Calculate(layout_input);
    const auto panel_left = (client_rect.right - layout.panel_width) / 2;
    const auto panel_top = layout.buttons_top - WindowGeometry::ScaleForDpi(20, dpi) - layout.panel_height;
    const RECT panel_rect{panel_left, panel_top, panel_left + layout.panel_width, panel_top + layout.panel_height};
    const auto text =
        IsSearchingSubfolders() ? message_ : message_ + L"\r\n\r\nYou can also drag an image or folder here.";
    DrawPanelText(dc, panel_rect, text, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX, dpi, light_theme, true);

    if (logo_ != nullptr && layout.logo_width > 0 && layout.logo_height > 0)
    {
        const auto logo_left = (client_rect.right - layout.logo_width) / 2;
        const auto logo_top = panel_top - layout.logo_height - WindowGeometry::ScaleForDpi(12, dpi);
        Gdiplus::Graphics graphics(dc);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::ImageAttributes image_attributes;
        image_attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        const Gdiplus::Rect destination(logo_left, logo_top, layout.logo_width, layout.logo_height);
        graphics.DrawImage(logo_.get(), destination, 0, 0, logo_->GetWidth(), logo_->GetHeight(), Gdiplus::UnitPixel,
                           &image_attributes);
    }
}

void EmptyStateView::DrawButton(const DRAWITEMSTRUCT* draw_item, UINT dpi, bool light_theme)
{
    if (draw_item == nullptr || draw_item->CtlType != ODT_BUTTON)
    {
        return;
    }

    wchar_t text[64]{};
    GetWindowText(draw_item->hwndItem, text, _countof(text));
    auto text_format = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
    if ((draw_item->itemState & ODS_NOACCEL) != 0)
    {
        text_format |= DT_HIDEPREFIX;
    }
    DrawPanelText(draw_item->hDC, draw_item->rcItem, text, text_format, dpi, light_theme, false);
    if ((draw_item->itemState & ODS_FOCUS) != 0)
    {
        GdiObject<HBRUSH> focus_brush(CreateSolidBrush(RGB(123, 104, 238)));
        if (focus_brush.valid())
        {
            auto focus_rect = draw_item->rcItem;
            const auto focus_width = std::max(2, WindowGeometry::ScaleForDpi(3, dpi));
            for (INT inset = 0; inset < focus_width && !IsRectEmpty(&focus_rect); ++inset)
            {
                FrameRect(draw_item->hDC, &focus_rect, focus_brush.get());
                InflateRect(&focus_rect, -1, -1);
            }
        }
    }
}

BOOL EmptyStateView::TranslateDialogMessage(MSG* message) const
{
    return message != nullptr && IsVisible() && parent_ != nullptr && IsDialogMessage(parent_, message);
}

HMENU EmptyStateView::CommandMenu(UINT command)
{
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(command));
}

HWND EmptyStateView::ButtonForCommand(UINT command) const
{
    switch (command)
    {
    case IDM_OPEN_IMAGE:
        return open_image_button_;
    case IDM_OPEN_FOLDER:
        return open_folder_button_;
    case IDM_SEARCH_SUBFOLDERS:
        return search_subfolders_button_;
    case IDM_EXIT:
        return exit_button_;
    default:
        return nullptr;
    }
}

bool EmptyStateView::IsButton(HWND window) const
{
    return window == open_image_button_ || window == open_folder_button_ || window == search_subfolders_button_ ||
           window == exit_button_;
}

void EmptyStateView::CreateCaptionFont()
{
    NONCLIENTMETRICS metrics{};
#if (WINVER >= 0x0600)
    metrics.cbSize = sizeof(NONCLIENTMETRICS) - sizeof(metrics.iPaddedBorderWidth);
#else
    metrics.cbSize = sizeof(NONCLIENTMETRICS);
#endif
    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &metrics, 0))
    {
        caption_font_.reset(CreateFontIndirect(&metrics.lfMessageFont));
    }
}

void EmptyStateView::LoadLogo()
{
    const auto resource = FindResource(instance_, MAKEINTRESOURCE(IDR_WELCOME_LOGO), RT_RCDATA);
    if (resource == nullptr)
    {
        return;
    }

    const auto resource_size = SizeofResource(instance_, resource);
    const auto resource_handle = LoadResource(instance_, resource);
    const auto resource_data = resource_handle == nullptr ? nullptr : LockResource(resource_handle);
    if (resource_size == 0 || resource_data == nullptr)
    {
        return;
    }

    GlobalMemory image_memory(GlobalAlloc(GMEM_MOVEABLE, resource_size));
    if (!image_memory.valid())
    {
        return;
    }

    const auto image_data = GlobalLock(image_memory.get());
    if (image_data == nullptr)
    {
        return;
    }

    std::memcpy(image_data, resource_data, resource_size);
    GlobalUnlock(image_memory.get());

    IStream* raw_image_stream{};
    if (FAILED(CreateStreamOnHGlobal(image_memory.get(), TRUE, &raw_image_stream)))
    {
        return;
    }
    image_memory.release();
    ComPtr<IStream> image_stream(raw_image_stream);

    auto image = std::make_unique<Gdiplus::Image>(image_stream.get(), FALSE);
    if (image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0 || image->GetHeight() == 0)
    {
        return;
    }

    logo_stream_ = std::move(image_stream);
    logo_ = std::move(image);
}

void EmptyStateView::DestroyControls()
{
    for (const auto button : {open_image_button_, open_folder_button_, search_subfolders_button_, exit_button_})
    {
        if (button != nullptr && IsWindow(button))
        {
            DestroyWindow(button);
        }
    }
}

void EmptyStateView::DrawPanelText(HDC dc, const RECT& panel_rect, const std::wstring& text, UINT text_format, UINT dpi,
                                   bool light_theme, bool vertically_center_text) const
{
    if (dc == nullptr || text.empty() || IsRectEmpty(&panel_rect))
    {
        return;
    }

    const auto width = panel_rect.right - panel_rect.left;
    const auto height = panel_rect.bottom - panel_rect.top;
    CompatibleDeviceContext memory_dc(CreateCompatibleDC(dc));
    GdiObject<HBITMAP> bitmap(memory_dc.valid() ? CreateCompatibleBitmap(dc, width, height) : nullptr);
    if (!memory_dc.valid() || !bitmap.valid())
    {
        return;
    }

    SelectedGdiObject bitmap_selection(memory_dc.get(), bitmap.get());
    if (!bitmap_selection.valid())
    {
        return;
    }

    const auto colors = GetColors(light_theme);
    RECT background_rect{0, 0, width, height};
    GdiObject<HBRUSH> background(CreateSolidBrush(colors.background));
    if (background.valid())
    {
        FillRect(memory_dc.get(), &background_rect, background.get());
    }

    CompatibleDeviceContext panel_dc(CreateCompatibleDC(dc));
    GdiObject<HBITMAP> panel_bitmap(panel_dc.valid() ? CreateCompatibleBitmap(dc, width, height) : nullptr);
    if (panel_dc.valid() && panel_bitmap.valid())
    {
        SelectedGdiObject panel_bitmap_selection(panel_dc.get(), panel_bitmap.get());
        GdiObject<HBRUSH> panel(CreateSolidBrush(colors.panel));
        if (panel_bitmap_selection.valid() && panel.valid())
        {
            FillRect(panel_dc.get(), &background_rect, panel.get());
        }
        if (panel_bitmap_selection.valid())
        {
            BLENDFUNCTION blend{};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 128;
            AlphaBlend(memory_dc.get(), 0, 0, width, height, panel_dc.get(), 0, 0, width, height, blend);
        }
    }

    GdiObject<HPEN> border_pen(CreatePen(PS_SOLID, 1, colors.border));
    if (border_pen.valid())
    {
        SelectedGdiObject pen_selection(memory_dc.get(), border_pen.get());
        SelectedGdiObject brush_selection(memory_dc.get(), GetStockObject(NULL_BRUSH));
        Rectangle(memory_dc.get(), 0, 0, width, height);
    }

    SetBkMode(memory_dc.get(), TRANSPARENT);
    SetTextColor(memory_dc.get(), colors.text);
    const auto horizontal_padding = WindowGeometry::ScaleForDpi(8, dpi);
    const auto vertical_padding = WindowGeometry::ScaleForDpi(6, dpi);
    RECT text_rect{horizontal_padding, vertical_padding, width - horizontal_padding, height - vertical_padding};
    const auto text_font = const_cast<EmptyStateView*>(this)->GetTextFont(dpi);
    SelectedGdiObject font_selection;
    if (text_font != nullptr)
    {
        font_selection = SelectedGdiObject(memory_dc.get(), text_font);
    }
    if (vertically_center_text)
    {
        RECT measured_text{0, 0, text_rect.right - text_rect.left, 0};
        DrawText(memory_dc.get(), text.c_str(), -1, &measured_text, text_format | DT_CALCRECT);
        const auto available_height = text_rect.bottom - text_rect.top;
        const auto text_height = measured_text.bottom - measured_text.top;
        if (text_height < available_height)
        {
            text_rect.top += (available_height - text_height) / 2;
            text_rect.bottom = text_rect.top + text_height;
        }
    }
    DrawText(memory_dc.get(), text.c_str(), -1, &text_rect, text_format);
    BitBlt(dc, panel_rect.left, panel_rect.top, width, height, memory_dc.get(), 0, 0, SRCCOPY);
}

HFONT EmptyStateView::GetTextFont(UINT dpi)
{
    dpi = dpi == 0 ? WindowGeometry::kDefaultDpi : dpi;
    if (text_font_.valid() && text_font_dpi_ == dpi)
    {
        return text_font_.get();
    }

    text_font_.reset();
    text_font_dpi_ = 0;

    LOGFONT logfont{};
    logfont.lfHeight = -MulDiv(10, static_cast<INT>(dpi), 72);
    logfont.lfWeight = FW_NORMAL;
    logfont.lfOutPrecision = OUT_TT_PRECIS;
    logfont.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    BOOL font_smoothing{};
    UINT font_smoothing_type{};
    const auto clear_type_enabled = SystemParametersInfo(SPI_GETFONTSMOOTHING, 0, &font_smoothing, 0) &&
                                    font_smoothing &&
                                    SystemParametersInfo(SPI_GETFONTSMOOTHINGTYPE, 0, &font_smoothing_type, 0) &&
                                    font_smoothing_type == FE_FONTSMOOTHINGCLEARTYPE;
    logfont.lfQuality = clear_type_enabled ? CLEARTYPE_NATURAL_QUALITY : ANTIALIASED_QUALITY;
    lstrcpy(logfont.lfFaceName, L"Lucida Console");

    text_font_.reset(CreateFontIndirect(&logfont));
    text_font_dpi_ = dpi;
    return text_font_.get();
}

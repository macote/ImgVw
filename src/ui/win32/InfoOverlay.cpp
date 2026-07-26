#include "InfoOverlay.h"

#include "WindowGeometry.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace
{
struct OverlayColors
{
    COLORREF background;
    COLORREF panel;
    COLORREF border;
    COLORREF text;
};

OverlayColors GetOverlayColors(bool light_theme)
{
    return light_theme ? OverlayColors{RGB(245, 245, 245), RGB(255, 255, 255), RGB(140, 140, 140), RGB(50, 50, 50)}
                       : OverlayColors{RGB(0, 0, 0), RGB(0, 0, 0), RGB(90, 90, 90), RGB(180, 180, 180)};
}
} // namespace

InfoOverlay::~InfoOverlay()
{
    DeleteObject(font_);
}

std::wstring InfoOverlay::BuildStatsText(const InfoOverlayStatsSnapshot& snapshot)
{
    std::wstringstream text;
    std::size_t cached{};
    std::size_t queued{};
    std::size_t free_slots{};
    std::size_t maximum_slots{};
    for (const auto& target : snapshot.targets)
    {
        cached += target.queued + target.loading + target.ready + target.error;
        queued += target.loader_queued;
        free_slots += target.free_slots;
        maximum_slots += target.maximum_slots;
    }

    text << L"Found: " << snapshot.found_images << L"; Cached: " << cached << L"; Queued: " << queued << L"; Slots: "
         << free_slots << L"/" << maximum_slots;
    if (snapshot.has_free_bytes)
    {
        text << L"; Free: " << OverlayText::FormatByteSize(snapshot.free_bytes);
    }

    if (snapshot.slideshow_running)
    {
        text << L"\r\n--------------------------------------------------------------------------\r\n";
        text << L"Mode: " << (snapshot.random_slideshow ? L"Random" : L"Sequential") << L" slideshow; Cycle: "
             << snapshot.cycle_position << L" / " << snapshot.cycle_total;
        if (snapshot.cycle_total > 0)
        {
            text << L" (" << OverlayText::FormatPercent(snapshot.cycle_position, snapshot.cycle_total) << L")";
        }
    }

    text << L"\r\n";
    text << L"--------------------------------------------------------------------------\r\n";
    text << std::left << std::setw(12) << L"Size" << std::right << std::setw(8) << L"Ready" << std::setw(10)
         << L"Loaded" << std::setw(10) << L"Loading" << std::setw(10) << L"Queued" << std::setw(8) << L"Errors"
         << std::setw(10) << L"Used" << std::setw(3) << L"" << L"\r\n";
    text << L"--------------------------------------------------------------------------\r\n";

    for (const auto& target : snapshot.targets)
    {
        const auto total = target.queued + target.loading + target.ready + target.error;
        std::wstringstream size;
        size << target.width << L"x" << target.height;
        text << std::left << std::setw(12) << size.str() << std::right << std::setw(8)
             << OverlayText::FormatPercent(target.ready, total) << std::setw(10) << target.ready << std::setw(10)
             << target.loading << std::setw(10) << target.queued << std::setw(8) << target.error;
        OverlayText::WriteByteSizeColumn(text, target.used_bytes, 13);
        text << L"\r\n";
    }

    if (!snapshot.current_item_text.empty())
    {
        text << L"--------------------------------------------------------------------------\r\n";
        text << snapshot.current_item_text;
    }
    return text.str();
}

void InfoOverlay::ResetLayout()
{
    DeleteObject(font_);
    font_ = nullptr;
    font_dpi_ = 0;
    Clear();
}

RECT InfoOverlay::CalculateRectangle(HDC dc, const std::wstring& text, UINT dpi, INT client_width, INT client_height)
{
    const auto inset = WindowGeometry::ScaleForDpi(16, dpi);
    const auto horizontal_padding = WindowGeometry::ScaleForDpi(8, dpi);
    const auto vertical_padding = WindowGeometry::ScaleForDpi(6, dpi);
    const auto fallback_width = WindowGeometry::ScaleForDpi(800, dpi);
    const auto fallback_height = WindowGeometry::ScaleForDpi(600, dpi);
    const auto available_width =
        (std::max)(1, (client_width > 0 ? client_width : fallback_width) - inset * 2 - horizontal_padding * 2);
    const auto available_height =
        (std::max)(1, (client_height > 0 ? client_height : fallback_height) - inset * 2 - vertical_padding * 2);
    RECT text_rectangle{0, 0, available_width, available_height};
    const auto font = GetFont(dpi);
    const auto previous_font = font == nullptr ? nullptr : SelectObject(dc, font);
    DrawText(dc, text.c_str(), -1, &text_rectangle, DT_CALCRECT | DT_LEFT | DT_NOPREFIX);
    if (previous_font != nullptr)
    {
        SelectObject(dc, previous_font);
    }

    const auto text_width = (std::min)(available_width, static_cast<INT>(text_rectangle.right - text_rectangle.left));
    const auto text_height = (std::min)(available_height, static_cast<INT>(text_rectangle.bottom - text_rectangle.top));
    return {inset, inset, inset + text_width + horizontal_padding * 2, inset + text_height + vertical_padding * 2};
}

void InfoOverlay::Draw(HDC dc, const RECT& rectangle, const std::wstring& text,
                       const ImgItem::DisplayState& display_state, UINT dpi, bool light_theme, UINT text_format,
                       COLORREF fallback_background, BOOL vertically_center_text)
{
    if (text.empty() || IsRectEmpty(&rectangle))
    {
        return;
    }

    const auto width = rectangle.right - rectangle.left;
    const auto height = rectangle.bottom - rectangle.top;
    const auto memory_dc = CreateCompatibleDC(dc);
    const auto bitmap = memory_dc == nullptr ? nullptr : CreateCompatibleBitmap(dc, width, height);
    if (memory_dc == nullptr || bitmap == nullptr)
    {
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        return;
    }

    const auto previous_bitmap = SelectObject(memory_dc, bitmap);
    if (previous_bitmap == nullptr || previous_bitmap == HGDI_ERROR)
    {
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        return;
    }

    RECT background_rectangle{0, 0, width, height};
    const auto colors = GetOverlayColors(light_theme);
    const auto background_brush = CreateSolidBrush(fallback_background);
    if (background_brush != nullptr)
    {
        FillRect(memory_dc, &background_rectangle, background_brush);
        DeleteObject(background_brush);
    }

    if (display_state.status == ImgItem::Status::Ready && display_state.frame != nullptr)
    {
        RECT image_rectangle{display_state.frame->offsetx(), display_state.frame->offsety(),
                             display_state.frame->offsetx() + display_state.frame->width(),
                             display_state.frame->offsety() + display_state.frame->height()};
        RECT intersection{};
        if (IntersectRect(&intersection, &rectangle, &image_rectangle))
        {
            const auto image_bitmap = display_state.frame->GetBitmap();
            const auto source_dc = CreateCompatibleDC(dc);
            if (source_dc != nullptr)
            {
                const auto previous_source_bitmap = SelectObject(source_dc, image_bitmap.bitmap());
                if (previous_source_bitmap != nullptr && previous_source_bitmap != HGDI_ERROR)
                {
                    BitBlt(memory_dc, intersection.left - rectangle.left, intersection.top - rectangle.top,
                           intersection.right - intersection.left, intersection.bottom - intersection.top, source_dc,
                           intersection.left - image_rectangle.left, intersection.top - image_rectangle.top, SRCCOPY);
                    SelectObject(source_dc, previous_source_bitmap);
                }
                DeleteDC(source_dc);
            }
        }
    }

    const auto panel_dc = CreateCompatibleDC(dc);
    const auto panel_bitmap = panel_dc == nullptr ? nullptr : CreateCompatibleBitmap(dc, width, height);
    if (panel_dc != nullptr && panel_bitmap != nullptr)
    {
        const auto previous_panel_bitmap = SelectObject(panel_dc, panel_bitmap);
        const auto panel_brush = CreateSolidBrush(colors.panel);
        if (panel_brush != nullptr)
        {
            FillRect(panel_dc, &background_rectangle, panel_brush);
            DeleteObject(panel_brush);
        }

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 128;
        AlphaBlend(memory_dc, 0, 0, width, height, panel_dc, 0, 0, width, height, blend);
        SelectObject(panel_dc, previous_panel_bitmap);
    }
    DeleteObject(panel_bitmap);
    DeleteDC(panel_dc);

    const auto border_pen = CreatePen(PS_SOLID, 1, colors.border);
    if (border_pen != nullptr)
    {
        const auto previous_pen = SelectObject(memory_dc, border_pen);
        const auto previous_brush = SelectObject(memory_dc, GetStockObject(NULL_BRUSH));
        Rectangle(memory_dc, 0, 0, width, height);
        SelectObject(memory_dc, previous_brush);
        SelectObject(memory_dc, previous_pen);
        DeleteObject(border_pen);
    }

    SetBkMode(memory_dc, TRANSPARENT);
    SetTextColor(memory_dc, colors.text);
    const auto horizontal_padding = WindowGeometry::ScaleForDpi(8, dpi);
    const auto vertical_padding = WindowGeometry::ScaleForDpi(6, dpi);
    RECT text_rectangle{horizontal_padding, vertical_padding, width - horizontal_padding, height - vertical_padding};
    const auto font = GetFont(dpi);
    const auto previous_font = font == nullptr ? nullptr : SelectObject(memory_dc, font);
    if (vertically_center_text)
    {
        RECT measured_text{0, 0, text_rectangle.right - text_rectangle.left, 0};
        DrawText(memory_dc, text.c_str(), -1, &measured_text, text_format | DT_CALCRECT);
        const auto available_height = text_rectangle.bottom - text_rectangle.top;
        const auto text_height = measured_text.bottom - measured_text.top;
        if (text_height < available_height)
        {
            text_rectangle.top += (available_height - text_height) / 2;
            text_rectangle.bottom = text_rectangle.top + text_height;
        }
    }

    DrawText(memory_dc, text.c_str(), -1, &text_rectangle, text_format);
    if (previous_font != nullptr)
    {
        SelectObject(memory_dc, previous_font);
    }

    BitBlt(dc, rectangle.left, rectangle.top, width, height, memory_dc, 0, 0, SRCCOPY);
    SelectObject(memory_dc, previous_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
}

HFONT InfoOverlay::GetFont(UINT dpi)
{
    dpi = dpi == 0 ? WindowGeometry::kDefaultDpi : dpi;
    if (font_ != nullptr && font_dpi_ == dpi)
    {
        return font_;
    }

    DeleteObject(font_);
    font_ = nullptr;

    LOGFONT log_font{};
    log_font.lfHeight = -MulDiv(10, static_cast<INT>(dpi), 72);
    log_font.lfWeight = FW_NORMAL;
    log_font.lfOutPrecision = OUT_TT_PRECIS;
    log_font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    BOOL font_smoothing{};
    UINT font_smoothing_type{};
    const auto clear_type_enabled = SystemParametersInfo(SPI_GETFONTSMOOTHING, 0, &font_smoothing, 0) &&
                                    font_smoothing &&
                                    SystemParametersInfo(SPI_GETFONTSMOOTHINGTYPE, 0, &font_smoothing_type, 0) &&
                                    font_smoothing_type == FE_FONTSMOOTHINGCLEARTYPE;
    log_font.lfQuality = clear_type_enabled ? CLEARTYPE_NATURAL_QUALITY : ANTIALIASED_QUALITY;
    lstrcpy(log_font.lfFaceName, L"Lucida Console");

    font_ = CreateFontIndirect(&log_font);
    font_dpi_ = dpi;
    return font_;
}

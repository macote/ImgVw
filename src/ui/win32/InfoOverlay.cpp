#include "InfoOverlay.h"

#include "CompatibleDeviceContext.h"
#include "Localization.h"
#include "SelectedGdiObject.h"
#include "WindowGeometry.h"
#include "resource.h"

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

std::wstring InfoOverlay::BuildStatsText(const InfoOverlayStatsSnapshot& snapshot)
{
    const auto language = Localization::GetLanguage();
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

    text << Localization::FormatString(IDS_STATS_SUMMARY,
                                       {std::to_wstring(snapshot.found_images), std::to_wstring(cached),
                                        std::to_wstring(queued), std::to_wstring(free_slots),
                                        std::to_wstring(maximum_slots)});
    if (snapshot.has_free_bytes)
    {
        text << Localization::FormatString(IDS_STATS_FREE_SUFFIX,
                                           {OverlayText::FormatByteSize(snapshot.free_bytes, language)});
    }

    const auto col_size_header = Localization::GetString(IDS_STATS_COL_SIZE);
    const auto col_ready_header = Localization::GetString(IDS_STATS_COL_READY);
    const auto col_loaded_header = Localization::GetString(IDS_STATS_COL_LOADED);
    const auto col_loading_header = Localization::GetString(IDS_STATS_COL_LOADING);
    const auto col_queued_header = Localization::GetString(IDS_STATS_COL_QUEUED);
    const auto col_errors_header = Localization::GetString(IDS_STATS_COL_ERRORS);
    const auto col_used_header = Localization::GetString(IDS_STATS_COL_USED);

    const auto col_size_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(12, col_size_header.size() + 2));
    const auto col_ready_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(8, col_ready_header.size() + 2));
    const auto col_loaded_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(10, col_loaded_header.size() + 2));
    const auto col_loading_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(10, col_loading_header.size() + 2));
    const auto col_queued_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(10, col_queued_header.size() + 2));
    const auto col_errors_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(8, col_errors_header.size() + 2));
    const auto col_used_width =
        static_cast<std::streamsize>((std::max<std::size_t>)(13, col_used_header.size() + 3));

    const auto separator_width = static_cast<std::size_t>(col_size_width + col_ready_width + col_loaded_width +
                                                          col_loading_width + col_queued_width + col_errors_width +
                                                          col_used_width);
    const std::wstring separator(separator_width, L'-');

    if (snapshot.slideshow_running)
    {
        const auto mode_label =
            Localization::GetString(snapshot.random_slideshow ? IDS_STATS_MODE_RANDOM : IDS_STATS_MODE_SEQUENTIAL);
        text << L"\r\n" << separator << L"\r\n";
        text << Localization::FormatString(IDS_STATS_SLIDESHOW_LINE,
                                           {mode_label, std::to_wstring(snapshot.cycle_position),
                                            std::to_wstring(snapshot.cycle_total)});
        if (snapshot.cycle_total > 0)
        {
            text << Localization::FormatString(
                IDS_STATS_SLIDESHOW_PERCENT_SUFFIX,
                {OverlayText::FormatPercent(snapshot.cycle_position, snapshot.cycle_total, language)});
        }
    }

    text << L"\r\n" << separator << L"\r\n";
    text << std::left << std::setw(col_size_width) << col_size_header << std::right << std::setw(col_ready_width)
         << col_ready_header << std::setw(col_loaded_width) << col_loaded_header << std::setw(col_loading_width)
         << col_loading_header << std::setw(col_queued_width) << col_queued_header << std::setw(col_errors_width)
         << col_errors_header << std::setw(col_used_width - 3) << col_used_header << std::setw(3) << L"" << L"\r\n";
    text << separator << L"\r\n";

    for (const auto& target : snapshot.targets)
    {
        const auto total = target.queued + target.loading + target.ready + target.error;
        std::wstringstream size;
        size << target.width << L"x" << target.height;
        text << std::left << std::setw(col_size_width) << size.str() << std::right << std::setw(col_ready_width)
             << OverlayText::FormatPercent(target.ready, total, language) << std::setw(col_loaded_width)
             << target.ready << std::setw(col_loading_width) << target.loading << std::setw(col_queued_width)
             << target.queued << std::setw(col_errors_width) << target.error;
        OverlayText::WriteByteSizeColumn(text, target.used_bytes, col_used_width, language);
        text << L"\r\n";
    }

    if (!snapshot.current_item_text.empty())
    {
        text << separator << L"\r\n";
        text << snapshot.current_item_text;
    }
    return text.str();
}

void InfoOverlay::ResetLayout()
{
    font_.reset();
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
    SelectedGdiObject font_selection;
    if (font != nullptr)
    {
        font_selection = SelectedGdiObject(dc, font);
    }
    DrawText(dc, text.c_str(), -1, &text_rectangle, DT_CALCRECT | DT_LEFT | DT_NOPREFIX);

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

    RECT background_rectangle{0, 0, width, height};
    const auto colors = GetOverlayColors(light_theme);
    GdiObject<HBRUSH> background_brush(CreateSolidBrush(fallback_background));
    if (background_brush.valid())
    {
        FillRect(memory_dc.get(), &background_rectangle, background_brush.get());
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
            CompatibleDeviceContext source_dc(CreateCompatibleDC(dc));
            if (source_dc.valid())
            {
                SelectedGdiObject source_bitmap_selection(source_dc.get(), image_bitmap.bitmap());
                if (source_bitmap_selection.valid())
                {
                    BitBlt(memory_dc.get(), intersection.left - rectangle.left, intersection.top - rectangle.top,
                           intersection.right - intersection.left, intersection.bottom - intersection.top,
                           source_dc.get(), intersection.left - image_rectangle.left,
                           intersection.top - image_rectangle.top, SRCCOPY);
                }
            }
        }
    }

    CompatibleDeviceContext panel_dc(CreateCompatibleDC(dc));
    GdiObject<HBITMAP> panel_bitmap(panel_dc.valid() ? CreateCompatibleBitmap(dc, width, height) : nullptr);
    if (panel_dc.valid() && panel_bitmap.valid())
    {
        SelectedGdiObject panel_bitmap_selection(panel_dc.get(), panel_bitmap.get());
        GdiObject<HBRUSH> panel_brush(CreateSolidBrush(colors.panel));
        if (panel_bitmap_selection.valid() && panel_brush.valid())
        {
            FillRect(panel_dc.get(), &background_rectangle, panel_brush.get());
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
    RECT text_rectangle{horizontal_padding, vertical_padding, width - horizontal_padding, height - vertical_padding};
    const auto font = GetFont(dpi);
    SelectedGdiObject font_selection;
    if (font != nullptr)
    {
        font_selection = SelectedGdiObject(memory_dc.get(), font);
    }
    if (vertically_center_text)
    {
        RECT measured_text{0, 0, text_rectangle.right - text_rectangle.left, 0};
        DrawText(memory_dc.get(), text.c_str(), -1, &measured_text, text_format | DT_CALCRECT);
        const auto available_height = text_rectangle.bottom - text_rectangle.top;
        const auto text_height = measured_text.bottom - measured_text.top;
        if (text_height < available_height)
        {
            text_rectangle.top += (available_height - text_height) / 2;
            text_rectangle.bottom = text_rectangle.top + text_height;
        }
    }

    DrawText(memory_dc.get(), text.c_str(), -1, &text_rectangle, text_format);
    BitBlt(dc, rectangle.left, rectangle.top, width, height, memory_dc.get(), 0, 0, SRCCOPY);
}

HFONT InfoOverlay::GetFont(UINT dpi)
{
    dpi = dpi == 0 ? WindowGeometry::kDefaultDpi : dpi;
    if (font_.valid() && font_dpi_ == dpi)
    {
        return font_.get();
    }

    font_.reset();

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

    font_.reset(CreateFontIndirect(&log_font));
    font_dpi_ = dpi;
    return font_.get();
}

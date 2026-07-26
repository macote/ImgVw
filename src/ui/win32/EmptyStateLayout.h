#pragma once

#include "WindowGeometry.h"

#include <algorithm>

namespace EmptyStateLayout
{
struct Input
{
    INT client_width{};
    INT client_height{};
    UINT dpi{WindowGeometry::kDefaultDpi};
    BOOL no_images{};
    BOOL searching_subfolders{};
    INT logo_source_width{};
    INT logo_source_height{};
};

struct Layout
{
    INT panel_width{};
    INT panel_height{};
    INT logo_width{};
    INT logo_height{};
    INT buttons_top{};
};

inline Layout Calculate(const Input& input)
{
    const auto scale = [&input](INT value) { return WindowGeometry::ScaleForDpi(value, input.dpi); };
    Layout layout;
    const auto available_width = std::max(0, input.client_width - scale(48));
    layout.panel_width = std::min(available_width, scale(460));
    layout.panel_height = scale(input.no_images ? 132 : 96);

    if (input.logo_source_width > 0 && input.logo_source_height > 0)
    {
        layout.logo_width = MulDiv(layout.panel_width, 4, 5);
        layout.logo_height = MulDiv(layout.logo_width, input.logo_source_height, input.logo_source_width);
    }

    const auto logo_gap = layout.logo_height > 0 ? scale(12) : 0;
    const auto panel_button_gap = scale(20);
    const auto button_height = scale(28);
    const auto button_row_gap = scale(12);
    const auto button_rows = input.searching_subfolders ? 1 : 2;
    const auto content_height = layout.logo_height + logo_gap + layout.panel_height + panel_button_gap +
                                button_height * button_rows + button_row_gap * (button_rows - 1);
    const auto content_top = std::max(0, (std::max(0, input.client_height) - content_height) / 2);
    layout.buttons_top = content_top + layout.logo_height + logo_gap + layout.panel_height + panel_button_gap;
    return layout;
}
} // namespace EmptyStateLayout

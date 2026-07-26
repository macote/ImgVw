#pragma once

#include "ImgItem.h"
#include "OverlayText.h"
#include "GdiObject.h"

#include <Windows.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

struct InfoOverlayProgressState
{
    INT last_percent{-2};
    DWORD wait_start_tick{};
    bool visible{};
    std::wstring path;
};

struct InfoOverlayProgressInput
{
    bool waiting{};
    INT percent{};
    std::wstring path;
    DWORD current_tick{};
    DWORD debounce_in_milliseconds{};
    bool info_overlay_visible{};
};

struct InfoOverlayActions
{
    bool arm_progress_timer{};
    bool cancel_progress_timer{};
    bool refresh_overlay{};
    bool invalidate_all{};
};

struct InfoOverlayProgressUpdate
{
    InfoOverlayProgressState state;
    InfoOverlayActions actions;
};

struct InfoOverlayTargetStats
{
    INT width{};
    INT height{};
    std::size_t queued{};
    std::size_t loading{};
    std::size_t ready{};
    std::size_t error{};
    ULONGLONG used_bytes{};
    std::size_t loader_queued{};
    std::size_t free_slots{};
    std::size_t maximum_slots{};
};

struct InfoOverlayStatsSnapshot
{
    std::size_t found_images{};
    std::vector<InfoOverlayTargetStats> targets;
    bool has_free_bytes{};
    ULONGLONG free_bytes{};
    bool slideshow_running{};
    bool random_slideshow{};
    std::size_t cycle_position{};
    std::size_t cycle_total{};
    std::wstring current_item_text;
};

class InfoOverlay final
{
  public:
    ~InfoOverlay() = default;
    InfoOverlay() = default;
    InfoOverlay(const InfoOverlay&) = delete;
    InfoOverlay& operator=(const InfoOverlay&) = delete;

    const std::wstring& text() const
    {
        return text_;
    }

    const RECT& rectangle() const
    {
        return rectangle_;
    }

    bool SetContent(std::wstring text, const RECT& rectangle)
    {
        if (text_ == text && EqualRect(&rectangle_, &rectangle))
        {
            return false;
        }

        text_ = std::move(text);
        rectangle_ = rectangle;
        return true;
    }

    RECT Clear()
    {
        const auto previous_rectangle = rectangle_;
        text_.clear();
        SetRectEmpty(&rectangle_);
        return previous_rectangle;
    }

    void ResetLayout();
    RECT CalculateRectangle(HDC dc, const std::wstring& text, UINT dpi, INT client_width, INT client_height);
    void Draw(HDC dc, const RECT& rectangle, const std::wstring& text, const ImgItem::DisplayState& display_state,
              UINT dpi, bool light_theme, UINT text_format = DT_LEFT | DT_NOPREFIX,
              COLORREF fallback_background = RGB(0, 0, 0), BOOL vertically_center_text = FALSE);

    static std::wstring BuildItemText(const ImgItem* item, const std::wstring& filepath)
    {
        const auto ready = item != nullptr && item->status() == ImgItem::Status::Ready;
        const auto loading = item != nullptr && item->status() == ImgItem::Status::Loading;
        const auto percent = loading ? (std::max)(item->loadingprogresspercent(), 0) : 0;
        return OverlayText::BuildItemInfo(filepath, ready, loading, percent);
    }

    static std::wstring BuildStatsText(const InfoOverlayStatsSnapshot& snapshot);

    static InfoOverlayProgressUpdate CalculateProgressUpdate(const InfoOverlayProgressState& previous,
                                                             const InfoOverlayProgressInput& input)
    {
        InfoOverlayProgressUpdate update{previous, {}};
        if (!input.waiting)
        {
            update.actions.invalidate_all = previous.visible;
            update.actions.refresh_overlay = input.info_overlay_visible;
            update.actions.cancel_progress_timer = true;
            update.state = {};
            return update;
        }

        if (update.state.path != input.path)
        {
            update.state = {};
            update.state.path = input.path;
        }

        if (update.state.wait_start_tick == 0)
        {
            update.state.wait_start_tick = input.current_tick;
        }
        update.actions.arm_progress_timer = true;

        const auto visible = input.current_tick - update.state.wait_start_tick >= input.debounce_in_milliseconds;
        if (input.percent != update.state.last_percent || visible != update.state.visible)
        {
            update.state.last_percent = input.percent;
            update.state.visible = visible;
            update.actions.refresh_overlay = input.info_overlay_visible;
            update.actions.invalidate_all = !input.info_overlay_visible && visible;
        }
        return update;
    }

    InfoOverlayActions UpdateLoadingProgress(const InfoOverlayProgressInput& input)
    {
        auto update = CalculateProgressUpdate(progress_state_, input);
        progress_state_ = std::move(update.state);
        return update.actions;
    }

    bool loading_progress_visible() const
    {
        return progress_state_.visible;
    }

    bool stats_visible() const
    {
        return stats_visible_;
    }

    bool SetStatsVisible(bool visible)
    {
        if (stats_visible_ == visible)
        {
            return false;
        }
        stats_visible_ = visible;
        return true;
    }

  private:
    HFONT GetFont(UINT dpi);

    std::wstring text_;
    RECT rectangle_{};
    GdiObject<HFONT> font_;
    UINT font_dpi_{};
    InfoOverlayProgressState progress_state_;
    bool stats_visible_{};
};

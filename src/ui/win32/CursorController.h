#pragma once

#include <Windows.h>

enum class CursorVisibilityAction
{
    None,
    Show,
    Hide,
};

struct CursorActivityActions
{
    CursorVisibilityAction visibility{CursorVisibilityAction::None};
    bool arm_idle_timer{};
    bool cancel_idle_timer{};
    UINT idle_timer_delay{};
};

class CursorController final
{
  public:
    CursorVisibilityAction SetVisible(bool visible)
    {
        if (visibility_known_ && visible_ == visible)
        {
            return CursorVisibilityAction::None;
        }
        visibility_known_ = true;
        visible_ = visible;
        return visible ? CursorVisibilityAction::Show : CursorVisibilityAction::Hide;
    }

    bool SetCaptured(bool captured)
    {
        if (captured_ == captured)
        {
            return false;
        }
        captured_ = captured;
        return true;
    }

    CursorActivityActions OnMouseMove(const POINTS& point, LONGLONG counter)
    {
        if (!has_last_point_)
        {
            last_point_ = point;
            has_last_point_ = true;
            return {};
        }
        if (last_point_.x == point.x && last_point_.y == point.y)
        {
            return {};
        }

        last_point_ = point;
        last_activity_counter_ = counter;
        if (idle_timer_armed_)
        {
            return {SetVisible(true), false, false, 0};
        }
        idle_timer_armed_ = true;
        return {SetVisible(true), true, false, 0};
    }

    CursorActivityActions OnIdleTimer(LONGLONG counter, LONGLONG frequency, UINT timeout_milliseconds)
    {
        if (!idle_timer_armed_)
        {
            return {};
        }
        if (frequency <= 0 || counter < last_activity_counter_)
        {
            idle_timer_armed_ = false;
            return {CursorVisibilityAction::None, false, true, 0};
        }

        const auto elapsed_milliseconds = (counter - last_activity_counter_) * 1000 / frequency;
        if (elapsed_milliseconds < timeout_milliseconds)
        {
            return {CursorVisibilityAction::None, true, false,
                    timeout_milliseconds - static_cast<UINT>(elapsed_milliseconds)};
        }

        idle_timer_armed_ = false;
        return {SetVisible(false), false, true, 0};
    }

    bool CancelIdleTimer()
    {
        const auto was_armed = idle_timer_armed_;
        idle_timer_armed_ = false;
        return was_armed;
    }

  private:
    bool visibility_known_{};
    bool visible_{};
    bool captured_{};
    bool has_last_point_{};
    bool idle_timer_armed_{};
    POINTS last_point_{};
    LONGLONG last_activity_counter_{};
};

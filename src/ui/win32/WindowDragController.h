#pragma once

#include <Windows.h>

class WindowDragController final
{
  public:
    bool Begin(const POINT& screen_point, const RECT& window_rectangle)
    {
        if (window_rectangle.right <= window_rectangle.left || window_rectangle.bottom <= window_rectangle.top)
        {
            return false;
        }

        start_point_ = screen_point;
        start_rectangle_ = window_rectangle;
        active_ = true;
        return true;
    }

    bool active() const
    {
        return active_;
    }

    POINT CalculatePosition(const POINT& screen_point) const
    {
        return {start_rectangle_.left + (screen_point.x - start_point_.x),
                start_rectangle_.top + (screen_point.y - start_point_.y)};
    }

    bool End()
    {
        const auto was_active = active_;
        active_ = false;
        return was_active;
    }

  private:
    bool active_{};
    POINT start_point_{};
    RECT start_rectangle_{};
};

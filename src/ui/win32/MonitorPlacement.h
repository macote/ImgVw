#pragma once

#include <Windows.h>

struct WindowPlacementBounds
{
    INT x{};
    INT y{};
    INT width{};
    INT height{};
    bool valid{};
};

struct MonitorTransition
{
    bool changed{};
    bool apply_bounds{};
};

class MonitorPlacement final
{
  public:
    static WindowPlacementBounds FromRectangle(const RECT& rectangle)
    {
        const auto width = rectangle.right - rectangle.left;
        const auto height = rectangle.bottom - rectangle.top;
        return {rectangle.left, rectangle.top, width, height, width > 0 && height > 0};
    }

    void SetCurrent(HMONITOR monitor)
    {
        current_ = monitor;
    }

    HMONITOR current() const
    {
        return current_;
    }

    MonitorTransition OnMonitorChanged(HMONITOR monitor, bool dragging)
    {
        if (monitor == nullptr || monitor == current_)
        {
            return {};
        }
        current_ = monitor;
        return {true, !dragging};
    }

  private:
    HMONITOR current_{nullptr};
};

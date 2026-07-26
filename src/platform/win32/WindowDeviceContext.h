#pragma once

#include <Windows.h>

#include <utility>

class WindowDeviceContext final
{
  public:
    WindowDeviceContext() = default;
    WindowDeviceContext(HWND window, HDC dc) : window_(window), dc_(dc) {}
    ~WindowDeviceContext()
    {
        reset();
    }
    WindowDeviceContext(const WindowDeviceContext&) = delete;
    WindowDeviceContext& operator=(const WindowDeviceContext&) = delete;
    WindowDeviceContext(WindowDeviceContext&& other) noexcept : window_(other.window_), dc_(other.release())
    {
        other.window_ = nullptr;
    }
    WindowDeviceContext& operator=(WindowDeviceContext&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            window_ = other.window_;
            dc_ = other.release();
            other.window_ = nullptr;
        }

        return *this;
    }
    HDC get() const
    {
        return dc_;
    }
    bool valid() const
    {
        return dc_ != nullptr;
    }
    HDC release()
    {
        const auto dc = dc_;
        dc_ = nullptr;
        return dc;
    }
    void reset(HWND window = nullptr, HDC dc = nullptr)
    {
        if (dc_ != nullptr)
        {
            ReleaseDC(window_, dc_);
        }

        window_ = window;
        dc_ = dc;
    }

  private:
    HWND window_{nullptr};
    HDC dc_{nullptr};
};

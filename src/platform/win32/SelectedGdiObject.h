#pragma once

#include <Windows.h>
#include <utility>

class SelectedGdiObject final
{
  public:
    SelectedGdiObject() = default;
    SelectedGdiObject(HDC dc, HGDIOBJ object) : dc_(dc), previous_(SelectObject(dc, object)) {}
    ~SelectedGdiObject()
    {
        reset();
    }
    SelectedGdiObject(const SelectedGdiObject&) = delete;
    SelectedGdiObject& operator=(const SelectedGdiObject&) = delete;
    SelectedGdiObject(SelectedGdiObject&& other) noexcept : dc_(other.dc_), previous_(other.previous_)
    {
        other.dc_ = nullptr;
        other.previous_ = nullptr;
    }
    SelectedGdiObject& operator=(SelectedGdiObject&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            dc_ = other.dc_;
            previous_ = other.previous_;
            other.dc_ = nullptr;
            other.previous_ = nullptr;
        }
        return *this;
    }
    bool valid() const
    {
        return dc_ != nullptr && previous_ != nullptr && previous_ != HGDI_ERROR;
    }
    HGDIOBJ previous() const
    {
        return previous_;
    }
    HGDIOBJ release()
    {
        const auto previous = previous_;
        dc_ = nullptr;
        previous_ = nullptr;
        return previous;
    }
    void reset()
    {
        if (valid())
        {
            SelectObject(dc_, previous_);
        }
        dc_ = nullptr;
        previous_ = nullptr;
    }

  private:
    HDC dc_{nullptr};
    HGDIOBJ previous_{nullptr};
};

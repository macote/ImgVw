#pragma once

#include <Windows.h>
#include <utility>

class CompatibleDeviceContext final
{
  public:
    CompatibleDeviceContext() = default;
    explicit CompatibleDeviceContext(HDC dc) : dc_(dc) {}
    ~CompatibleDeviceContext()
    {
        reset();
    }
    CompatibleDeviceContext(const CompatibleDeviceContext&) = delete;
    CompatibleDeviceContext& operator=(const CompatibleDeviceContext&) = delete;
    CompatibleDeviceContext(CompatibleDeviceContext&& other) noexcept : dc_(other.release()) {}
    CompatibleDeviceContext& operator=(CompatibleDeviceContext&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
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
    void reset(HDC dc = nullptr)
    {
        if (dc_ == dc)
        {
            return;
        }
        if (dc_ != nullptr)
        {
            DeleteDC(dc_);
        }
        dc_ = dc;
    }

  private:
    HDC dc_{nullptr};
};

#pragma once

#include <Windows.h>
#include <utility>

class Win32Handle final
{
  public:
    Win32Handle() = default;
    explicit Win32Handle(HANDLE handle) : handle_(handle) {}
    ~Win32Handle()
    {
        reset();
    }
    Win32Handle(const Win32Handle&) = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;
    Win32Handle(Win32Handle&& other) noexcept : handle_(other.release()) {}
    Win32Handle& operator=(Win32Handle&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }

        return *this;
    }
    HANDLE get() const
    {
        return handle_;
    }
    bool valid() const
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release()
    {
        const auto handle = handle_;
        handle_ = nullptr;
        return handle;
    }
    void reset(HANDLE handle = nullptr)
    {
        if (handle_ == handle)
        {
            return;
        }

        if (valid())
        {
            CloseHandle(handle_);
        }

        handle_ = handle;
    }

  private:
    HANDLE handle_{nullptr};
};

#pragma once

#include <Windows.h>
#include <utility>

class FindHandle final
{
  public:
    FindHandle() = default;
    explicit FindHandle(HANDLE handle) : handle_(handle) {}
    ~FindHandle()
    {
        reset();
    }
    FindHandle(const FindHandle&) = delete;
    FindHandle& operator=(const FindHandle&) = delete;
    FindHandle(FindHandle&& other) noexcept : handle_(other.release()) {}
    FindHandle& operator=(FindHandle&& other) noexcept
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
        return handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release()
    {
        const auto handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return handle;
    }
    void reset(HANDLE handle = INVALID_HANDLE_VALUE)
    {
        if (handle_ == handle)
        {
            return;
        }

        if (valid())
        {
            FindClose(handle_);
        }

        handle_ = handle;
    }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

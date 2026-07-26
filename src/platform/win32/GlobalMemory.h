#pragma once

#include <Windows.h>

#include <utility>

class GlobalMemory final
{
  public:
    GlobalMemory() = default;
    explicit GlobalMemory(HGLOBAL memory) : memory_(memory) {}
    ~GlobalMemory()
    {
        reset();
    }
    GlobalMemory(const GlobalMemory&) = delete;
    GlobalMemory& operator=(const GlobalMemory&) = delete;
    GlobalMemory(GlobalMemory&& other) noexcept : memory_(other.release()) {}
    GlobalMemory& operator=(GlobalMemory&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }

        return *this;
    }
    HGLOBAL get() const
    {
        return memory_;
    }
    bool valid() const
    {
        return memory_ != nullptr;
    }
    HGLOBAL release()
    {
        const auto memory = memory_;
        memory_ = nullptr;
        return memory;
    }
    void reset(HGLOBAL memory = nullptr)
    {
        if (memory_ == memory)
        {
            return;
        }

        if (memory_ != nullptr)
        {
            GlobalFree(memory_);
        }

        memory_ = memory;
    }

  private:
    HGLOBAL memory_{nullptr};
};

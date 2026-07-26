#pragma once

#include <Windows.h>

#include <utility>

class RegistryKey final
{
  public:
    RegistryKey() = default;
    explicit RegistryKey(HKEY key) : key_(key) {}
    ~RegistryKey()
    {
        reset();
    }
    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;
    RegistryKey(RegistryKey&& other) noexcept : key_(other.release()) {}
    RegistryKey& operator=(RegistryKey&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }

        return *this;
    }
    HKEY get() const
    {
        return key_;
    }
    HKEY* put()
    {
        reset();
        return &key_;
    }
    bool valid() const
    {
        return key_ != nullptr;
    }
    HKEY release()
    {
        const auto key = key_;
        key_ = nullptr;
        return key;
    }
    void reset(HKEY key = nullptr)
    {
        if (key_ == key)
        {
            return;
        }

        if (key_ != nullptr)
        {
            RegCloseKey(key_);
        }

        key_ = key;
    }

  private:
    HKEY key_{nullptr};
};

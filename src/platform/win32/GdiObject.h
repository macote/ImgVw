#pragma once

#include <Windows.h>
#include <utility>

template <typename T> class GdiObject final
{
  public:
    GdiObject() = default;
    explicit GdiObject(T object) : object_(object) {}
    ~GdiObject()
    {
        reset();
    }
    GdiObject(const GdiObject&) = delete;
    GdiObject& operator=(const GdiObject&) = delete;
    GdiObject(GdiObject&& other) noexcept : object_(other.release()) {}
    GdiObject& operator=(GdiObject&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }
    T get() const
    {
        return object_;
    }
    bool valid() const
    {
        return object_ != nullptr;
    }
    T release()
    {
        const auto object = object_;
        object_ = nullptr;
        return object;
    }
    void reset(T object = nullptr)
    {
        if (object_ == object)
        {
            return;
        }
        if (object_ != nullptr)
        {
            DeleteObject(object_);
        }
        object_ = object;
    }

  private:
    T object_{nullptr};
};

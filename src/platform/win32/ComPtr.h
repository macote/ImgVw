#pragma once

#include <ObjBase.h>

#include <utility>

template <typename T> class ComPtr final
{
  public:
    ComPtr() = default;
    explicit ComPtr(T* pointer) : pointer_(pointer) {}
    ~ComPtr()
    {
        reset();
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : pointer_(other.release()) {}
    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }

        return *this;
    }
    T* get() const
    {
        return pointer_;
    }
    T* operator->() const
    {
        return pointer_;
    }
    bool valid() const
    {
        return pointer_ != nullptr;
    }
    T* release()
    {
        const auto pointer = pointer_;
        pointer_ = nullptr;
        return pointer;
    }
    void reset(T* pointer = nullptr)
    {
        if (pointer_ == pointer)
        {
            return;
        }

        if (pointer_ != nullptr)
        {
            pointer_->Release();
        }

        pointer_ = pointer;
    }

  private:
    T* pointer_{nullptr};
};

template <typename T> class CoTaskMemPtr final
{
  public:
    CoTaskMemPtr() = default;
    explicit CoTaskMemPtr(T* pointer) : pointer_(pointer) {}
    ~CoTaskMemPtr()
    {
        reset();
    }
    CoTaskMemPtr(const CoTaskMemPtr&) = delete;
    CoTaskMemPtr& operator=(const CoTaskMemPtr&) = delete;
    CoTaskMemPtr(CoTaskMemPtr&& other) noexcept : pointer_(other.release()) {}
    CoTaskMemPtr& operator=(CoTaskMemPtr&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }

        return *this;
    }
    T* get() const
    {
        return pointer_;
    }
    bool valid() const
    {
        return pointer_ != nullptr;
    }
    T* release()
    {
        const auto pointer = pointer_;
        pointer_ = nullptr;
        return pointer;
    }
    void reset(T* pointer = nullptr)
    {
        if (pointer_ == pointer)
        {
            return;
        }

        CoTaskMemFree(pointer_);
        pointer_ = pointer;
    }

  private:
    T* pointer_{nullptr};
};

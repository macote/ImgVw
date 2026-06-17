#pragma once

#include <Windows.h>
#include <algorithm>
#include <stdexcept>

class ImgBitmap
{
  public:
    ImgBitmap(const PBITMAPINFO pbitmapinfo, const PBYTE buffer, INT buffersize)
    {
        Initialize(pbitmapinfo, buffer, buffersize);
    }
    ~ImgBitmap()
    {
        DeleteBitmap();
    }
    ImgBitmap(const ImgBitmap&) = delete;
    ImgBitmap(ImgBitmap&& other)
    {
        *this = std::move(other);
    }
    ImgBitmap& operator=(ImgBitmap&& other)
    {
        if (this != &other)
        {
            DeleteBitmap();

            bitmap_ = other.bitmap_;
            other.bitmap_ = nullptr;
        }

        return *this;
    }
    HBITMAP bitmap() const
    {
        return bitmap_;
    }

  private:
    HBITMAP bitmap_{nullptr};

  private:
    void Initialize(const PBITMAPINFO pbitmapinfo, const PBYTE buffer, INT buffersize);
    void DeleteBitmap();
};

inline void ImgBitmap::DeleteBitmap()
{
    if (bitmap_ != nullptr)
    {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
}

inline void ImgBitmap::Initialize(const PBITMAPINFO pbitmapinfo, const PBYTE buffer, INT buffersize)
{
    const auto dc = GetDC(NULL);
    if (dc == NULL)
    {
        throw std::runtime_error("ImgBitmap.Initialize(GetDC()) failed.");
    }

    const auto usage = pbitmapinfo->bmiHeader.biClrUsed > 0 ? DIB_PAL_COLORS : DIB_RGB_COLORS;
    PBYTE bits{nullptr};
    bitmap_ = CreateDIBSection(dc, pbitmapinfo, usage, reinterpret_cast<void**>(&bits), NULL, 0);
    ReleaseDC(NULL, dc);
    if (bitmap_ == nullptr || bits == nullptr)
    {
        throw std::runtime_error("ImgBitmap.Initialize(CreateDIBSection()) failed.");
    }

    CopyMemory(bits, buffer, buffersize);
}

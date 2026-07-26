#pragma once

#include "WindowDeviceContext.h"

#include <Windows.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

class ImgBitmap
{
  public:
    ImgBitmap(const BITMAPINFO* bitmapinfo, const BYTE* buffer, DWORD buffersize)
    {
        Initialize(bitmapinfo, buffer, buffersize);
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
    void Initialize(const BITMAPINFO* bitmapinfo, const BYTE* buffer, DWORD buffersize);
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

inline void ImgBitmap::Initialize(const BITMAPINFO* bitmapinfo, const BYTE* buffer, DWORD buffersize)
{
    if (bitmapinfo == nullptr || buffer == nullptr || buffersize == 0 ||
        bitmapinfo->bmiHeader.biCompression != BI_RGB || bitmapinfo->bmiHeader.biBitCount != 24 ||
        bitmapinfo->bmiHeader.biWidth <= 0 || bitmapinfo->bmiHeader.biHeight == 0)
    {
        throw std::invalid_argument("ImgBitmap.Initialize() received invalid bitmap data.");
    }

    const auto width = static_cast<unsigned long long>(bitmapinfo->bmiHeader.biWidth);
    const auto signedheight = static_cast<long long>(bitmapinfo->bmiHeader.biHeight);
    const auto height = static_cast<unsigned long long>(signedheight < 0 ? -signedheight : signedheight);
    const auto stride = ((width * 3ULL) + 3ULL) & ~3ULL;
    const auto expectedsize = stride * height;
    if (expectedsize > (std::numeric_limits<DWORD>::max)() || expectedsize != buffersize)
    {
        throw std::invalid_argument("ImgBitmap.Initialize() buffer size does not match its bitmap geometry.");
    }

    WindowDeviceContext dc(nullptr, GetDC(nullptr));
    if (!dc.valid())
    {
        throw std::runtime_error("ImgBitmap.Initialize(GetDC()) failed.");
    }

    const auto usage = bitmapinfo->bmiHeader.biClrUsed > 0 ? DIB_PAL_COLORS : DIB_RGB_COLORS;
    PBYTE bits{nullptr};
    bitmap_ = CreateDIBSection(dc.get(), bitmapinfo, usage, reinterpret_cast<void**>(&bits), nullptr, 0);
    if (bitmap_ == nullptr || bits == nullptr)
    {
        throw std::runtime_error("ImgBitmap.Initialize(CreateDIBSection()) failed.");
    }

    CopyMemory(bits, buffer, buffersize);
}

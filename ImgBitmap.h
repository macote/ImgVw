#pragma once

#include "Window.h"
#include <algorithm>

class ImgBitmap
{
public:
    ImgBitmap()
    {
    }
    ImgBitmap(PBITMAPINFO pbitmapinfo, PBYTE buffer, INT buffersize)
    {
        Initialize(pbitmapinfo, buffer, buffersize);
    }
    ~ImgBitmap()
    {
        DeleteBitmap();
    }
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
    HBITMAP bitmap() const { return bitmap_; }
private:
    void Initialize(PBITMAPINFO pbitmapinfo, PBYTE buffer, INT buffersize);
    void DeleteBitmap();
private:
    HBITMAP bitmap_{ nullptr };
};

inline void ImgBitmap::DeleteBitmap()
{
    if (bitmap_ != nullptr)
    {
        DeleteObject(bitmap_);
    }
}

inline void ImgBitmap::Initialize(PBITMAPINFO pbitmapinfo, PBYTE buffer, INT buffersize)
{
    auto dc = GetDC(NULL);
    auto usage = pbitmapinfo->bmiHeader.biClrUsed > 0 ? DIB_PAL_COLORS : DIB_RGB_COLORS;
    PBYTE bits;
    bitmap_ = CreateDIBSection(dc, pbitmapinfo, usage, (void**)&bits, NULL, 0);
    ReleaseDC(NULL, dc);

    CopyMemory(bits, buffer, buffersize);
}
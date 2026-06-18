#include "ImgItemHelper.h"
#include "ExifOrientation.h"
#include <vector>

namespace
{
class SemaphoreGuard
{
  public:
    SemaphoreGuard(const CountingSemaphore& semaphore, bool acquire) : semaphore_(semaphore), acquired_(acquire)
    {
        if (acquired_)
        {
            semaphore_.Wait();
        }
    }

    ~SemaphoreGuard()
    {
        if (acquired_)
        {
            semaphore_.Notify();
        }
    }

    SemaphoreGuard(const SemaphoreGuard&) = delete;
    SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

  private:
    const CountingSemaphore& semaphore_;
    bool acquired_;
};
} // namespace

const CountingSemaphore ImgItemHelper::kGDIOperationSemaphore = CountingSemaphore(kGDIOperationSemaphoreCount);

ImgItem::Format ImgItemHelper::GetImgFormatFromExtension(const std::wstring& filepath)
{
    const auto extension = PathFindExtension(filepath.c_str());
    if (CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".jpg", -1) == CSTR_EQUAL ||
        CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".jpeg", -1) == CSTR_EQUAL)
    {
        return ImgItem::Format::JPEG;
    }
    else if (CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".png", -1) == CSTR_EQUAL)
    {
        return ImgItem::Format::PNG;
    }
    else if (CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".bmp", -1) == CSTR_EQUAL ||
             CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".gif", -1) == CSTR_EQUAL ||
             CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".ico", -1) == CSTR_EQUAL ||
             CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".tif", -1) == CSTR_EQUAL ||
             CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".tiff", -1) == CSTR_EQUAL)
    {
        return ImgItem::Format::Other;
    }

    return ImgItem::Format::Unsupported;
}

ImgBuffer ImgItemHelper::Resize24bppRGBImage(INT width, INT height, const PBYTE buffer, INT targetwidth,
                                             INT targetheight)
{
    return ResizeAndRotate24bppRGBImage(width, height, buffer, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone);
}

ImgBuffer ImgItemHelper::ResizeAndRotate24bppRGBImage(INT width, INT height, const PBYTE buffer, INT targetwidth,
                                                      INT targetheight, Gdiplus::RotateFlipType rotateflip)
{
    auto bitmaptoresize = Get24bppRGBBitmap(width, height, buffer);

    return ResizeAndRotateImage(bitmaptoresize.get(), targetwidth, targetheight, rotateflip);
}

ImgBuffer ImgItemHelper::Rotate24bppRGBImage(INT width, INT height, const PBYTE buffer,
                                             Gdiplus::RotateFlipType rotateflip)
{
    auto bitmaptorotate = Get24bppRGBBitmap(width, height, buffer);

    return RotateImage(bitmaptorotate.get(), rotateflip);
}

ImgBuffer ImgItemHelper::ResizeImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight)
{
    return ResizeAndRotateImage(bitmap, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone);
}

ImgBuffer ImgItemHelper::ResizeAndRotateImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
                                              Gdiplus::RotateFlipType rotateflip)
{
    const auto percentWidth = static_cast<FLOAT>(targetwidth) / bitmap->GetWidth();
    const auto percentHeight = static_cast<FLOAT>(targetheight) / bitmap->GetHeight();
    const auto percent = percentHeight < percentWidth ? percentHeight : percentWidth;
    const auto newwidth = static_cast<INT>(bitmap->GetWidth() * percent);
    const auto newheight = static_cast<INT>(bitmap->GetHeight() * percent);
    const SemaphoreGuard semaphore_guard(kGDIOperationSemaphore, true);
    auto resizedbitmap = std::make_unique<Gdiplus::Bitmap>(newwidth, newheight, PixelFormat24bppRGB);
    Gdiplus::Graphics graphics(resizedbitmap.get());
    graphics.DrawImage(bitmap, 0, 0, newwidth, newheight);

    return RotateImage(resizedbitmap.get(), rotateflip, TRUE);
}

ImgBuffer ImgItemHelper::RotateImage(Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip, BOOL gdiinuse)
{
    const SemaphoreGuard semaphore_guard(kGDIOperationSemaphore, !gdiinuse);

    if (rotateflip != Gdiplus::RotateNoneFlipNone)
    {
        if (bitmap->RotateFlip(rotateflip) != Gdiplus::Ok)
        {
            throw std::runtime_error("ImgItemHelper.RotateImage(Bitmap.RotateFlip()) failed.");
        }
    }

    auto buffer = GetBuffer(bitmap);

    return buffer;
}

std::unique_ptr<Gdiplus::Bitmap> ImgItemHelper::Get24bppRGBBitmap(INT width, INT height, const PBYTE buffer)
{
    BITMAPINFO bitmapinfo{};
    bitmapinfo.bmiHeader.biCompression = BI_RGB;
    bitmapinfo.bmiHeader.biBitCount = 24;
    bitmapinfo.bmiHeader.biWidth = width;
    bitmapinfo.bmiHeader.biHeight = height;
    bitmapinfo.bmiHeader.biPlanes = 1;
    bitmapinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

    return std::make_unique<Gdiplus::Bitmap>(&bitmapinfo, buffer);
}

ImgBuffer ImgItemHelper::GetBuffer(Gdiplus::Bitmap* bitmap)
{
    Gdiplus::BitmapData data{};
    const Gdiplus::Rect rect(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data) != Gdiplus::Ok)
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.LockBits()) failed.");
    }

    const auto stride = data.Stride < 0 ? -data.Stride : data.Stride;
    std::vector<BYTE> bottom_up_buffer(static_cast<std::size_t>(stride) * data.Height);
    const auto source = static_cast<const BYTE*>(data.Scan0);
    for (UINT row = 0; row < data.Height; ++row)
    {
        CopyMemory(bottom_up_buffer.data() + static_cast<std::size_t>(data.Height - row - 1) * stride,
                   source + static_cast<ptrdiff_t>(row) * data.Stride, stride);
    }

    if (bitmap->UnlockBits(&data) != Gdiplus::Ok)
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.UnlockBits()) failed.");
    }

    ImgBuffer buffer;
    buffer.WriteData(data.Width, data.Height, stride, bottom_up_buffer.data());

    return buffer;
}

UINT ImgItemHelper::GetExifOrientationFromData(const PBYTE exifdata, UINT exifdatabytecount)
{
    return exif::GetOrientation(exifdata, exifdatabytecount);
}

Gdiplus::RotateFlipType ImgItemHelper::GetRotateFlipTypeFromExifOrientation(UINT exiforientation)
{
    switch (exiforientation)
    {

        //   1       2       3       4         5           6           7           8
        //
        // 888888  888888      88  88      8888888888  88                  88  8888888888
        // 88          88      88  88      88  88      88  88          88  88      88  88
        // 8888      8888    8888  8888    88          8888888888  8888888888          88
        // 88          88      88  88
        // 88          88  888888  888888

    case 1:
        return Gdiplus::RotateNoneFlipNone;
    case 2:
        return Gdiplus::RotateNoneFlipX;
    case 3:
        return Gdiplus::Rotate180FlipNone;
    case 4:
        return Gdiplus::Rotate180FlipX;
    case 5:
        return Gdiplus::Rotate90FlipX;
    case 6:
        return Gdiplus::Rotate90FlipNone;
    case 7:
        return Gdiplus::Rotate270FlipX;
    case 8:
        return Gdiplus::Rotate270FlipNone;
    default:
        return Gdiplus::RotateNoneFlipNone;
    }
}

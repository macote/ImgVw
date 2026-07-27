#include "ImgItemHelper.h"
#include "ExifOrientation.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
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
            acquired_ = semaphore_.Wait() == CountingSemaphoreWaitStatus::Acquired;
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

class BitmapLock final
{
  public:
    BitmapLock(Gdiplus::Bitmap* bitmap, const Gdiplus::Rect& rectangle) : bitmap_(bitmap)
    {
        locked_ = bitmap_ != nullptr &&
                  bitmap_->LockBits(&rectangle, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data_) == Gdiplus::Ok;
    }
    ~BitmapLock()
    {
        Unlock();
    }
    BitmapLock(const BitmapLock&) = delete;
    BitmapLock& operator=(const BitmapLock&) = delete;
    bool locked() const
    {
        return locked_;
    }
    const Gdiplus::BitmapData& data() const
    {
        return data_;
    }
    bool Unlock()
    {
        if (!locked_)
        {
            return true;
        }

        locked_ = false;
        return bitmap_->UnlockBits(&data_) == Gdiplus::Ok;
    }

  private:
    Gdiplus::Bitmap* bitmap_{};
    Gdiplus::BitmapData data_{};
    bool locked_{};
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
    else if (CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".heic", -1) == CSTR_EQUAL ||
             CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".heif", -1) == CSTR_EQUAL ||
             CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, extension, -1, L".hif", -1) == CSTR_EQUAL)
    {
        return ImgItem::Format::HEIF;
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

bool ImgItemHelper::CalculateDisplaySize(INT width, INT height, INT targetwidth, INT targetheight, INT* displaywidth,
                                         INT* displayheight)
{
    if (width <= 0 || height <= 0 || targetwidth <= 0 || targetheight <= 0 || displaywidth == nullptr ||
        displayheight == nullptr)
    {
        return false;
    }

    *displaywidth = width;
    *displayheight = height;
    if (width <= targetwidth && height <= targetheight)
    {
        return true;
    }

    const auto widthscale = static_cast<double>(targetwidth) / width;
    const auto heightscale = static_cast<double>(targetheight) / height;
    const auto scale = (std::min)(widthscale, heightscale);
    *displaywidth = (std::max)(1, static_cast<INT>(width * scale));
    *displayheight = (std::max)(1, static_cast<INT>(height * scale));
    return true;
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
    if (bitmap == nullptr)
    {
        throw std::invalid_argument("ImgItemHelper.ResizeAndRotateImage() received a null bitmap.");
    }

    INT newwidth{};
    INT newheight{};
    if (!CalculateDisplaySize(bitmap->GetWidth(), bitmap->GetHeight(), targetwidth, targetheight, &newwidth,
                              &newheight))
    {
        throw std::runtime_error("ImgItemHelper.ResizeAndRotateImage() received invalid dimensions.");
    }
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
    if (width <= 0 || height <= 0 || buffer == nullptr || width > ((std::numeric_limits<INT>::max)() - 3) / 3)
    {
        throw std::invalid_argument("ImgItemHelper.Get24bppRGBBitmap() received invalid bitmap data.");
    }

    BITMAPINFO bitmapinfo{};
    bitmapinfo.bmiHeader.biCompression = BI_RGB;
    bitmapinfo.bmiHeader.biBitCount = 24;
    bitmapinfo.bmiHeader.biWidth = width;
    bitmapinfo.bmiHeader.biHeight = height;
    bitmapinfo.bmiHeader.biPlanes = 1;
    bitmapinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(&bitmapinfo, buffer);
    if (bitmap->GetLastStatus() != Gdiplus::Ok)
    {
        throw std::runtime_error("ImgItemHelper.Get24bppRGBBitmap(Bitmap) failed.");
    }

    return bitmap;
}

ImgBuffer ImgItemHelper::GetBuffer(Gdiplus::Bitmap* bitmap)
{
    if (bitmap == nullptr || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0 ||
        bitmap->GetWidth() > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
        bitmap->GetHeight() > static_cast<UINT>((std::numeric_limits<INT>::max)()))
    {
        throw std::invalid_argument("ImgItemHelper.GetBuffer() received invalid bitmap dimensions.");
    }

    const Gdiplus::Rect rect(0, 0, static_cast<INT>(bitmap->GetWidth()), static_cast<INT>(bitmap->GetHeight()));
    BitmapLock lock(bitmap, rect);
    if (!lock.locked())
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.LockBits()) failed.");
    }

    const auto& data = lock.data();
    if (data.Scan0 == nullptr || data.Width == 0 || data.Height == 0 ||
        data.Width > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
        data.Height > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
        data.Stride == (std::numeric_limits<INT>::min)())
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.LockBits()) returned invalid bitmap data.");
    }

    if (static_cast<std::size_t>(data.Width) > ((std::numeric_limits<std::size_t>::max)() - 3U) / 3U)
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.LockBits()) returned an unsupported bitmap layout.");
    }

    const auto stride = data.Stride < 0 ? -data.Stride : data.Stride;
    const auto minimum_stride = ((static_cast<std::size_t>(data.Width) * 3U) + 3U) & ~std::size_t(3U);
    if (stride <= 0 || static_cast<std::size_t>(stride) < minimum_stride ||
        static_cast<std::size_t>(stride) > (std::numeric_limits<DWORD>::max)() / data.Height ||
        (data.Height > 1 && static_cast<std::size_t>(stride) >
                                static_cast<std::size_t>((std::numeric_limits<ptrdiff_t>::max)()) / (data.Height - 1)))
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.LockBits()) returned an unsupported bitmap layout.");
    }

    const auto buffer_size = static_cast<std::size_t>(stride) * data.Height;
    std::vector<BYTE> bottom_up_buffer(buffer_size);
    const auto source = static_cast<const BYTE*>(data.Scan0);
    for (UINT row = 0; row < data.Height; ++row)
    {
        CopyMemory(bottom_up_buffer.data() + static_cast<std::size_t>(data.Height - row - 1) * stride,
                   source + static_cast<ptrdiff_t>(row) * data.Stride, stride);
    }

    if (!lock.Unlock())
    {
        throw std::runtime_error("ImgItemHelper.GetBuffer(Bitmap.UnlockBits()) failed.");
    }

    ImgBuffer buffer;
    buffer.WriteData(data.Width, data.Height, stride, bottom_up_buffer.data());

    return buffer;
}

UINT ImgItemHelper::GetExifOrientationFromData(const BYTE* exifdata, UINT exifdatabytecount)
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

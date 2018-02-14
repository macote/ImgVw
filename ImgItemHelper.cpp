#include "ImgItemHelper.h"

const CountingSemaphore ImgItemHelper::kGDIOperationSemaphore = CountingSemaphore(kGDIOperationSemaphoreCount);

ImgItem::Format ImgItemHelper::GetImgFormatFromExtension(const std::wstring& filepath)
{
    const auto extension = PathFindExtension(filepath.c_str());
    if (StrCmpI(extension, L".jpg") == 0 || StrCmpI(extension, L".jpeg") == 0)
    {
        return ImgItem::Format::JPEG;
    }
    else if (StrCmpI(extension, L".png") == 0)
    {
        return ImgItem::Format::PNG;
    }
    else if (StrCmpI(extension, L".bmp") == 0
        || StrCmpI(extension, L".gif") == 0
        || StrCmpI(extension, L".ico") == 0
        || StrCmpI(extension, L".tif") == 0
        || StrCmpI(extension, L".tiff") == 0)
    {
        return ImgItem::Format::Other;
    }

    return ImgItem::Format::Unsupported;
}

ImgBuffer ImgItemHelper::Resize24bppRGBImage(INT width, INT height, const PBYTE buffer, INT targetwidth, INT targetheight)
{
    return ResizeAndRotate24bppRGBImage(width, height, buffer, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone);
}

ImgBuffer ImgItemHelper::ResizeAndRotate24bppRGBImage(INT width, INT height, const PBYTE buffer, INT targetwidth, INT targetheight,
    Gdiplus::RotateFlipType rotateflip)
{
    auto bitmaptoresize = Get24bppRGBBitmap(width, height, buffer);

    return ResizeAndRotateImage(bitmaptoresize.get(), targetwidth, targetheight, rotateflip);
}

ImgBuffer ImgItemHelper::Rotate24bppRGBImage(INT width, INT height, const PBYTE buffer, Gdiplus::RotateFlipType rotateflip)
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
    auto percentWidth = static_cast<FLOAT>(targetwidth) / bitmap->GetWidth();
    auto percentHeight = static_cast<FLOAT>(targetheight) / bitmap->GetHeight();
    auto percent = percentHeight < percentWidth ? percentHeight : percentWidth;
    auto newwidth = static_cast<INT>(bitmap->GetWidth() * percent);
    auto newheight = static_cast<INT>(bitmap->GetHeight() * percent);
    ImgItemHelper::kGDIOperationSemaphore.Wait();
    std::unique_ptr<Gdiplus::Bitmap> resizedbitmap = std::make_unique<Gdiplus::Bitmap>(newwidth, newheight, PixelFormat24bppRGB);
    Gdiplus::Graphics graphics(resizedbitmap.get());
    graphics.DrawImage(bitmap, 0, 0, newwidth, newheight);
    auto buffer = RotateImage(resizedbitmap.get(), rotateflip, TRUE);
    ImgItemHelper::kGDIOperationSemaphore.Notify();
    
    return buffer;
}

ImgBuffer ImgItemHelper::RotateImage(Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip, BOOL gdiinuse)
{
    if (!gdiinuse)
    {
        ImgItemHelper::kGDIOperationSemaphore.Wait();
    }

    if (rotateflip != Gdiplus::RotateNoneFlipNone)
    {
        bitmap->RotateFlip(rotateflip);
    }

    auto buffer = GetBuffer(bitmap);

    if (!gdiinuse)
    {
        ImgItemHelper::kGDIOperationSemaphore.Notify();
    }

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
    Gdiplus::Rect rect(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
    bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data);
    ImgBuffer buffer;
    buffer.WriteData(data.Width, data.Height, data.Stride, reinterpret_cast<PBYTE>(data.Scan0));
    bitmap->UnlockBits(&data);

    return buffer;
}

UINT ImgItemHelper::GetExifOrientationFromData(const PBYTE exifdata, UINT exifdatabytecount)
{
    easyexif::EXIFInfo exifinfo;
    exifinfo.parseFromEXIFSegment(exifdata, exifdatabytecount);

    return exifinfo.Orientation;
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
        return Gdiplus::Rotate270FlipX;
    case 6:
        return Gdiplus::Rotate270FlipNone;
    case 7:
        return Gdiplus::Rotate90FlipX;
    case 8:
        return Gdiplus::Rotate90FlipNone;
    default:
        return Gdiplus::RotateNoneFlipNone;
    }
}
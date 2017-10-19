#include "ImgItemHelper.h"

CountingSemaphore ImgItemHelper::kGDIOperationSemaphore = CountingSemaphore(kGDIOperationSemaphoreCount);

ImgItem::Format ImgItemHelper::GetImgFormatFromExtension(std::wstring filepath)
{
    auto extension = PathFindExtension(filepath.c_str());
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

void ImgItemHelper::Resize24bppRGBImage(ImgItem& imgitem, INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight)
{
    ResizeAndRotate24bppRGBImage(imgitem, width, height, buffer, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone);
}

void ImgItemHelper::ResizeAndRotate24bppRGBImage(ImgItem& imgitem, INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight,
    Gdiplus::RotateFlipType rotateflip)
{
    auto bitmaptoresize = Get24bppRGBBitmap(width, height, buffer);
    ResizeAndRotateImage(imgitem, bitmaptoresize.get(), targetwidth, targetheight, rotateflip);
}

void ImgItemHelper::Rotate24bppRGBImage(ImgItem& imgitem, INT width, INT height, PBYTE buffer, Gdiplus::RotateFlipType rotateflip)
{
    auto bitmaptorotate = Get24bppRGBBitmap(width, height, buffer);
    RotateImage(imgitem, bitmaptorotate.get(), rotateflip);
}

void ImgItemHelper::ResizeImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight)
{
    ResizeAndRotateImage(imgitem, bitmap, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone);
}

void ImgItemHelper::ResizeAndRotateImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
    Gdiplus::RotateFlipType rotateflip)
{
    auto percentWidth = (float)targetwidth / bitmap->GetWidth();
    auto percentHeight = (float)targetheight / bitmap->GetHeight();
    auto percent = percentHeight < percentWidth ? percentHeight : percentWidth;
    auto newwidth = (int)(bitmap->GetWidth() * percent);
    auto newheight = (int)(bitmap->GetHeight() * percent);
    ImgItemHelper::kGDIOperationSemaphore.Wait();
    std::unique_ptr<Gdiplus::Bitmap> resizedbitmap = std::make_unique<Gdiplus::Bitmap>(newwidth, newheight, PixelFormat24bppRGB);
    Gdiplus::Graphics graphics(resizedbitmap.get());
    graphics.DrawImage(bitmap, 0, 0, newwidth, newheight);
    RotateImage(imgitem, resizedbitmap.get(), rotateflip, TRUE);
    ImgItemHelper::kGDIOperationSemaphore.Notify();
}

void ImgItemHelper::RotateImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip, BOOL gdiinuse)
{
    if (!gdiinuse)
    {
        ImgItemHelper::kGDIOperationSemaphore.Wait();
    }

    if (rotateflip != Gdiplus::RotateNoneFlipNone)
    {
        bitmap->RotateFlip(rotateflip);
    }

    HandleBuffer(imgitem, bitmap);
    
    if (!gdiinuse)
    {
        ImgItemHelper::kGDIOperationSemaphore.Notify();
    }
}

std::unique_ptr<Gdiplus::Bitmap> ImgItemHelper::Get24bppRGBBitmap(INT width, INT height, PBYTE buffer)
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

void ImgItemHelper::HandleBuffer(ImgItem& imgitem, Gdiplus::Bitmap* bitmap)
{
    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
    bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data);
    imgitem.HandleBuffer(data.Width, data.Height, data.Stride, (PBYTE)data.Scan0);
    bitmap->UnlockBits(&data);
}

UINT ImgItemHelper::GetExifOrientationFromData(PBYTE exifdata, UINT exifdatabytecount)
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
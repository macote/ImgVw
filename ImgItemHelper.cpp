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

void ImgItemHelper::Resize24bppRGBImage(INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight,
    std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    ResizeAndRotate24bppRGBImage(width, height, buffer, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone, bufferhandler);
}

void ImgItemHelper::ResizeAndRotate24bppRGBImage(INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight,
    Gdiplus::RotateFlipType rotateflip,
    std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    auto bitmaptoresize = Get24bppRGBBitmap(width, height, buffer);
    ResizeAndRotateImage(bitmaptoresize.get(), targetwidth, targetheight, rotateflip, bufferhandler);
}

void ImgItemHelper::Rotate24bppRGBImage(INT width, INT height, PBYTE buffer, Gdiplus::RotateFlipType rotateflip,
    std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    auto bitmaptorotate = Get24bppRGBBitmap(width, height, buffer);
    RotateImage(bitmaptorotate.get(), rotateflip, bufferhandler);
}

void ImgItemHelper::ResizeImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
    std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    ResizeAndRotateImage(bitmap, targetwidth, targetheight, Gdiplus::RotateNoneFlipNone, bufferhandler);
}

void ImgItemHelper::ResizeAndRotateImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
    Gdiplus::RotateFlipType rotateflip,
    std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    auto percentWidth = (float)targetwidth / bitmap->GetWidth();
    auto percentHeight = (float)targetheight / bitmap->GetHeight();
    auto percent = percentHeight < percentWidth ? percentHeight : percentWidth;
    auto newwidth = (int)(bitmap->GetWidth() * percent);
    auto newheight = (int)(bitmap->GetHeight() * percent);
    ImgItemHelper::kGDIOperationSemaphore.Wait();
    std::unique_ptr<Gdiplus::Bitmap> resizedbitmap = std::make_unique<Gdiplus::Bitmap>(newwidth, newheight, bitmap->GetPixelFormat());
    Gdiplus::Graphics graphics(resizedbitmap.get());
    graphics.DrawImage(bitmap, 0, 0, newwidth, newheight);
    if (rotateflip != Gdiplus::RotateNoneFlipNone)
    {
        resizedbitmap->RotateFlip(rotateflip);
        newwidth = resizedbitmap->GetWidth();
        newheight = resizedbitmap->GetHeight();
    }

    HandleBuffer(resizedbitmap.get(), bufferhandler);
    ImgItemHelper::kGDIOperationSemaphore.Notify();
}

void ImgItemHelper::RotateImage(Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip,
    std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    ImgItemHelper::kGDIOperationSemaphore.Wait();
    if (rotateflip != Gdiplus::RotateNoneFlipNone)
    {
        bitmap->RotateFlip(rotateflip);
    }

    HandleBuffer(bitmap, bufferhandler);
    ImgItemHelper::kGDIOperationSemaphore.Notify();
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

void ImgItemHelper::HandleBuffer(Gdiplus::Bitmap* bitmap, std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
    bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data);
    bufferhandler(data);
    bitmap->UnlockBits(&data);
}
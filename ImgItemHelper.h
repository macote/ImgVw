#pragma once

#include "ImgItem.h"
#include "CountingSemaphore.h"
#include "3rd-party\easyexif\exif.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <Gdiplus.h>
#include <memory>

class ImgItemHelper
{
public:
    static ImgItem::Format GetImgFormatFromExtension(std::wstring filepath);
    static void Resize24bppRGBImage(ImgItem& imgitem, INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight);
    static void ResizeAndRotate24bppRGBImage(ImgItem& imgitem, INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight,
        Gdiplus::RotateFlipType rotateflip);
    static void Rotate24bppRGBImage(ImgItem& imgitem, INT width, INT height, PBYTE buffer, Gdiplus::RotateFlipType rotateflip);
    static void ResizeImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight);
    static void ResizeAndRotateImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
        Gdiplus::RotateFlipType rotateflip);
    static void RotateImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip)
    {
        RotateImage(imgitem, bitmap, rotateflip, FALSE);
    };
    static std::unique_ptr<Gdiplus::Bitmap> Get24bppRGBBitmap(INT width, INT height, PBYTE buffer);
    static UINT GetExifOrientationFromData(PBYTE exifdata, UINT exifdatabytecount);
    static Gdiplus::RotateFlipType GetRotateFlipTypeFromExifOrientation(UINT exiforientation);
private:
    static void RotateImage(ImgItem& imgitem, Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip, BOOL gdiinuse);
    static void HandleBuffer(ImgItem& imgitem, Gdiplus::Bitmap* bitmap);
    static const INT kGDIOperationSemaphoreCount = 1;
    static CountingSemaphore kGDIOperationSemaphore;
};
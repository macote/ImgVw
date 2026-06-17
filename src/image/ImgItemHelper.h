#pragma once

#include "ImgBuffer.h"
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
    static ImgItem::Format GetImgFormatFromExtension(const std::wstring& filepath);
    static ImgBuffer Resize24bppRGBImage(INT width, INT height, const PBYTE buffer, INT targetwidth, INT targetheight);
    static ImgBuffer ResizeAndRotate24bppRGBImage(INT width, INT height, const PBYTE buffer, INT targetwidth, INT targetheight,
        Gdiplus::RotateFlipType rotateflip);
    static ImgBuffer Rotate24bppRGBImage(INT width, INT height, const PBYTE buffer, Gdiplus::RotateFlipType rotateflip);
    static ImgBuffer ResizeImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight);
    static ImgBuffer ResizeAndRotateImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
        Gdiplus::RotateFlipType rotateflip);
    static ImgBuffer RotateImage(Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip)
    {
        return RotateImage(bitmap, rotateflip, FALSE);
    };
    static std::unique_ptr<Gdiplus::Bitmap> Get24bppRGBBitmap(INT width, INT height, const PBYTE buffer);
    static UINT GetExifOrientationFromData(const PBYTE exifdata, UINT exifdatabytecount);
    static Gdiplus::RotateFlipType GetRotateFlipTypeFromExifOrientation(UINT exiforientation);
    static ImgBuffer RotateImage(Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip, BOOL gdiinuse);
    static ImgBuffer GetBuffer(Gdiplus::Bitmap* bitmap);
private:
    static const INT kGDIOperationSemaphoreCount = 1;
    static const CountingSemaphore kGDIOperationSemaphore;
};
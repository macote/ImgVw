#pragma once

#include "ImgItem.h"
#include "CountingSemaphore.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <Gdiplus.h>
#include <functional>
#include <memory>

class ImgItemHelper
{
public:
	static ImgItem::Format GetImgFormatFromExtension(std::wstring filepath);
	static void Resize24bppRGBImage(INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
	static void ResizeAndRotate24bppRGBImage(INT width, INT height, PBYTE buffer, INT targetwidth, INT targetheight,
		Gdiplus::RotateFlipType rotateflip,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
	static void Rotate24bppRGBImage(INT width, INT height, PBYTE buffer, Gdiplus::RotateFlipType rotateflip,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
	static void ResizeImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
	static void ResizeAndRotateImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
		Gdiplus::RotateFlipType rotateflip,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
	static void RotateImage(Gdiplus::Bitmap* bitmap, Gdiplus::RotateFlipType rotateflip,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
private:
	static std::unique_ptr<Gdiplus::Bitmap> Get24bppRGBBitmap(INT width, INT height, PBYTE buffer);
	static void HandleBuffer(Gdiplus::Bitmap* bitmap, std::function<void(Gdiplus::BitmapData&)> bufferhandler);
	static const INT kGDIOperationSemaphoreCount = 1;
	static CountingSemaphore kGDIOperationSemaphore;
};
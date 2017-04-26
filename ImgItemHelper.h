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
	static void ResizeImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
		std::function<void(Gdiplus::BitmapData&)> bufferhandler);
private:
	static const INT kGDIResizeSemaphoreCount = 1;
	static CountingSemaphore kGDIResizeSemaphore;
};
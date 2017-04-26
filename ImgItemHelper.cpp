#include "ImgItemHelper.h"

CountingSemaphore ImgItemHelper::kGDIResizeSemaphore = CountingSemaphore(kGDIResizeSemaphoreCount);

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
	BITMAPINFO bitmapinfo{};
	bitmapinfo.bmiHeader.biCompression = BI_RGB;
	bitmapinfo.bmiHeader.biBitCount = 24;
	bitmapinfo.bmiHeader.biWidth = width;
	bitmapinfo.bmiHeader.biHeight = height;
	bitmapinfo.bmiHeader.biPlanes = 1;
	bitmapinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	std::unique_ptr<Gdiplus::Bitmap> bitmaptoresize = std::make_unique<Gdiplus::Bitmap>(&bitmapinfo, buffer);
	ResizeImage(bitmaptoresize.get(), targetwidth, targetheight, bufferhandler);
}

void ImgItemHelper::ResizeImage(Gdiplus::Bitmap* bitmap, INT targetwidth, INT targetheight,
	std::function<void(Gdiplus::BitmapData&)> bufferhandler)
{
	auto percentWidth = (float)targetwidth / bitmap->GetWidth();
	auto percentHeight = (float)targetheight / bitmap->GetHeight();
	auto percent = percentHeight < percentWidth ? percentHeight : percentWidth;
	auto newwidth = (int)(bitmap->GetWidth() * percent);
	auto newheight = (int)(bitmap->GetHeight() * percent);
	ImgItemHelper::kGDIResizeSemaphore.Wait();
	std::unique_ptr<Gdiplus::Bitmap> resizedbitmap = std::make_unique<Gdiplus::Bitmap>(newwidth, newheight, bitmap->GetPixelFormat());
	Gdiplus::Graphics graphics(resizedbitmap.get());
	graphics.DrawImage(bitmap, 0, 0, newwidth, newheight);
	Gdiplus::BitmapData data{};
	Gdiplus::Rect rect(0, 0, newwidth, newheight);
	resizedbitmap.get()->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data);
	bufferhandler(data);
	resizedbitmap.get()->UnlockBits(&data);
	ImgItemHelper::kGDIResizeSemaphore.Notify();
}
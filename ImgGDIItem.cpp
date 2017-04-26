#include "ImgGDIItem.h"
#include "ImgBitmap.h"
#include "ImgItemHelper.h"

void ImgGDIItem::Load()
{
	status_ = Status::Loading;
	std::unique_ptr<Gdiplus::Bitmap> bitmap;

	if (filepath_.size() == 0)
	{
		status_ = Status::Error;
		goto done;
	}

	bitmap = std::make_unique<Gdiplus::Bitmap>(filepath_.c_str(), FALSE);
	lastgdiplusstatus_ = bitmap->GetLastStatus();
	if (lastgdiplusstatus_ != Gdiplus::Status::Ok || bitmap->GetWidth() == 0)
	{
		status_ = Status::Error;
		goto done;
	}

	width_ = bitmap->GetWidth();
	height_ = bitmap->GetHeight();
	if (width_ > targetwidth_ || height_ > targetheight_)
	{
		ImgItemHelper::ResizeImage(bitmap.get(), targetwidth_, targetheight_,
			[&](Gdiplus::BitmapData& data)
		{
			displaywidth_ = data.Width;
			displayheight_ = data.Height;
			buffersize_ = data.Height * data.Stride;
			this->WriteTempFile((PBYTE)data.Scan0, buffersize_);
		});
	}
	else
	{
		Gdiplus::BitmapData data{};
		Gdiplus::Rect rect(0, 0, width_, height_);
		bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data);
		displaywidth_ = data.Width;
		displayheight_ = data.Height;
		buffersize_ = data.Height * data.Stride;
		this->WriteTempFile((PBYTE)data.Scan0, buffersize_);
		bitmap->UnlockBits(&data);
	}

	SetupRGBColorsBITMAPINFO(24, displaywidth_, -displayheight_); // stored image is top-down, hence the negative height

	offsetx_ = (targetwidth_ - displaywidth_) / 2;
	offsety_ = (targetheight_ - displayheight_) / 2;

	status_ = Status::Ready;

done:

	SetEvent(loadedevent_);
}

void ImgGDIItem::Unload()
{
	lastgdiplusstatus_ = Gdiplus::Status::Ok;
	ImgItem::Unload();
}
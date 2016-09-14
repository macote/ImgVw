#include "ImgItem.h"

void ImgItem::Load()
{
	status_ = Status::Loading;
	Gdiplus::Bitmap* displaybitmap;
	std::unique_ptr<Gdiplus::Bitmap> resizedgdibitmap;

	if (filepath_.size() == 0)
	{
		status_ = Status::Error;
		goto complete;
	}

	gdibitmap_ = std::make_unique<Gdiplus::Bitmap>(filepath_.c_str(), FALSE);
	lastgdiplusstatus_ = gdibitmap_->GetLastStatus();
	if (lastgdiplusstatus_ != Gdiplus::Status::Ok || gdibitmap_->GetWidth() == 0)
	{
		status_ = Status::Error;
		goto complete;
	}

	displaybitmap = gdibitmap_.get();
	width_ = displaybitmap->GetWidth();
	height_ = displaybitmap->GetHeight();
	if (width_ > targetwidth_ || height_ > targetheight_)
	{
		auto percentWidth = (float)targetwidth_ / width_;
		auto percentHeight = (float)targetheight_ / height_;
		float percent = percentHeight < percentWidth ? percentHeight : percentWidth;
		displaywidth_ = (int)(width_ * percent);
		offsetx_ = (targetwidth_ - displaywidth_) / 2;
		displayheight_ = (int)(height_ * percent);
		offsety_ = (targetheight_ - displayheight_) / 2;

		resizedgdibitmap.reset(new Gdiplus::Bitmap(displaywidth_, displayheight_, displaybitmap->GetPixelFormat()));
		Gdiplus::Graphics graphics(resizedgdibitmap.get());
		lastgdiplusstatus_ = graphics.DrawImage(displaybitmap, 0, 0, displaywidth_, displayheight_);
		if (lastgdiplusstatus_ != Gdiplus::Status::Ok)
		{
			status_ = Status::Error;
			goto complete;
		}

		displaybitmap = resizedgdibitmap.get();
	}
	else
	{
		offsetx_ = (targetwidth_ - width_) / 2;
		displaywidth_ = width_;
		offsety_ = (targetheight_ - height_) / 2;
		displayheight_ = height_;
	}

	lastgdiplusstatus_ = displaybitmap->GetHBITMAP(Gdiplus::Color::MakeARGB(0, 0, 0, 0), &displaybitmap_);
	if (lastgdiplusstatus_ != Gdiplus::Status::Ok)
	{
		status_ = Status::Error;
		goto complete;
	}

	status_ = Status::Ready;

complete:

	resizedgdibitmap.reset();
	SetEvent(loadedevent_);
}

void ImgItem::Unload()
{
	gdibitmap_.reset();
	DeleteObject(displaybitmap_);
	lastgdiplusstatus_ = Gdiplus::Status::Ok;
	status_ = Status::Queued;
	ResetEvent(loadedevent_);
}

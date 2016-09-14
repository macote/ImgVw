#pragma once

#include "CountSemaphore.h"
#include "TimestampLogger.h"
#include <Windows.h>
#include <objidl.h>
#include <Gdiplus.h>
#include <string>
#include <memory>

class ImgItem
{
public:
	enum class Status
	{
		Queued,
		Loading,
		Ready,
		Error
	};
	ImgItem(std::wstring filepath, INT targetwidth, INT targetheight)
		: filepath_(filepath), targetwidth_(targetwidth), targetheight_(targetheight)
	{
		loadedevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
	}
	~ImgItem()
	{
		CloseHandle(loadedevent_);
		if (displaybitmap_ != nullptr)
		{
			DeleteObject(displaybitmap_);
		}
	}
	void Load();
	void Unload();
	Status status() const { return status_; }
	Gdiplus::Status lastgdiplusstatus() const { return lastgdiplusstatus_; }
	HANDLE displaybitmap() const { return displaybitmap_; }
	INT displaywidth() const { return displaywidth_; }
	INT displayheight() const { return displayheight_; }
	INT offsetx() const { return offsetx_; }
	INT offsety() const { return offsety_; }
	HANDLE loadedevent() const { return loadedevent_; }
private:
	std::wstring filepath_;
	INT targetwidth_;
	INT targetheight_;
	INT displaywidth_{};
	INT displayheight_{};
	INT width_{};
	INT height_{};
	INT offsetx_{};
	INT offsety_{};
	Status status_{ Status::Queued };
	Gdiplus::Status lastgdiplusstatus_{ Gdiplus::Status::Ok };
	HBITMAP displaybitmap_{ nullptr };
	HANDLE loadedevent_{ NULL };
	std::unique_ptr<Gdiplus::Bitmap> gdibitmap_;
};

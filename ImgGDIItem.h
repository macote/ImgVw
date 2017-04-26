#pragma once

#include "ImgItem.h"
#include <Windows.h>
#include <Gdiplus.h>
#include <string>

class ImgGDIItem : public ImgItem
{
public:
	ImgGDIItem(std::wstring filepath, std::wstring temppath, INT targetwidth, INT targetheight)
		: ImgItem(filepath, temppath, targetwidth, targetheight)
	{
	}
	virtual ~ImgGDIItem()
	{
	}
	void Load();
	void Unload();
	Gdiplus::Status lastgdiplusstatus() const { return lastgdiplusstatus_; }
private:
	Gdiplus::Status lastgdiplusstatus_{ Gdiplus::Status::Ok };
};
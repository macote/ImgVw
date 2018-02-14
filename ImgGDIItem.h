#pragma once

#include "ImgItem.h"
#include <Windows.h>
#include <Gdiplus.h>
#include <string>

class ImgGDIItem final : public ImgItem
{
public:
    ImgGDIItem(std::wstring filepath, INT targetwidth, INT targetheight)
        : ImgItem(filepath, targetwidth, targetheight) { }
    ImgGDIItem(const ImgGDIItem&) = delete;
    ImgGDIItem& operator=(const ImgGDIItem&) = delete;
    void Load();
    void Unload();
    Gdiplus::Status lastgdiplusstatus() const { return lastgdiplusstatus_; }
private:
    Gdiplus::Status lastgdiplusstatus_{ Gdiplus::Status::Ok };
};
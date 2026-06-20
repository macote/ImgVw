#pragma once

#include "ImgJpegDecoder.h"
#include "ImgItem.h"
#include <Windows.h>
#include <Gdiplus.h>
#include <string>

class ImgJPEGItem final : public ImgItem
{
  public:
    ImgJPEGItem(std::wstring filepath, INT targetwidth, INT targetheight) : ImgItem(filepath, targetwidth, targetheight)
    {
    }
    ImgJPEGItem(const ImgJPEGItem&) = delete;
    ImgJPEGItem& operator=(const ImgJPEGItem&) = delete;
    void Load();

  private:
    std::string errorstring_;
};

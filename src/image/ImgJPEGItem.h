#pragma once

#include "ImgJPEGDecoder.h"
#include "ImgItem.h"
#include <Windows.h>
#include <Gdiplus.h>
#include <string>

class ImgJPEGItem final : public ImgItem
{
  public:
    ImgJPEGItem(std::wstring filepath, INT targetwidth, INT targetheight);
    ImgJPEGItem(const ImgJPEGItem&) = delete;
    ImgJPEGItem& operator=(const ImgJPEGItem&) = delete;
    void Load();

  private:
    static void UpdateDecodeProgress(int percent, void* context);

    std::string errorstring_;
};

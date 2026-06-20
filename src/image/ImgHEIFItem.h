#pragma once

#include "ImgItem.h"

#include <Windows.h>
#include <string>

class ImgHEIFItem final : public ImgItem
{
  public:
    ImgHEIFItem(std::wstring filepath, INT targetwidth, INT targetheight) : ImgItem(filepath, targetwidth, targetheight)
    {
    }
    ImgHEIFItem(const ImgHEIFItem&) = delete;
    ImgHEIFItem& operator=(const ImgHEIFItem&) = delete;
    void Load() override;
};

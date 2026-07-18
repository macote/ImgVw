#pragma once

#include "ImgItem.h"
#include <string>

class ImageFormatResolver
{
  public:
    static ImgItem::Format Resolve(const std::wstring& filepath);
};

#pragma once

#include "ImgItem.h"
#include <string>

class ImgFormatResolver
{
  public:
    static ImgItem::Format Resolve(const std::wstring& filepath);
};

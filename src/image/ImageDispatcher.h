#pragma once

#include "ImgItem.h"
#include <memory>
#include <string>

class ImageDispatcher
{
  public:
    static std::shared_ptr<ImgItem> Create(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                           ImgItem::Format format);
};

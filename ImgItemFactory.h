#pragma once

#include "ImgItem.h"
#include "ImgItemHelper.h"
#include "ImgJPEGItem.h"
#include "ImgGDIItem.h"
#include <string>
#include <memory>

class ImgItemFactory
{
public:
    static std::shared_ptr<ImgItem> Create(const std::wstring& filepath, INT targetwidth, INT targetheight)
    {
        const auto imgformat = ImgItemHelper::GetImgFormatFromExtension(filepath);
        switch (imgformat)
        {
        case ImgItem::Format::JPEG:
            return std::make_shared<ImgJPEGItem>(filepath, targetwidth, targetheight);
            break;
        case ImgItem::Format::PNG:
            return std::make_shared<ImgGDIItem>(filepath, targetwidth, targetheight);
            break;
        case ImgItem::Format::Other:
            return std::make_shared<ImgGDIItem>(filepath, targetwidth, targetheight);
            break;
        default:
            throw std::runtime_error("ImgItemFactory::Create(): the specified image format is not supported.");
        }
    }
};

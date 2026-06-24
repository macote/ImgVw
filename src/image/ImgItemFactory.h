#pragma once

#include "ImageFormatDetector.h"
#include "ImageHeaderProbe.h"
#include "ImgItem.h"
#include "ImgItemHelper.h"
#include "ImgJPEGItem.h"
#include "ImgGDIItem.h"
#include "ImgHEIFItem.h"
#include <string>
#include <memory>

class ImgItemFactory
{
  public:
    static ImgItem::Format ResolveFormat(const std::wstring& filepath)
    {
        const auto extensionformat = ImgItemHelper::GetImgFormatFromExtension(filepath);
        if (extensionformat == ImgItem::Format::Unsupported)
        {
            return ImgItem::Format::Unsupported;
        }

        const auto probe = ImageHeaderProbe::ReadPrefix(filepath);
        if (probe.Succeeded())
        {
            const auto detectedformat = ImageFormatDetector::ToImgItemFormat(
                ImageFormatDetector::Detect(probe.bytes.data(), probe.bytes.size()));
            if (detectedformat != ImgItem::Format::Unsupported)
            {
                return detectedformat;
            }
        }

        return extensionformat;
    }

    static std::shared_ptr<ImgItem> Create(const std::wstring& filepath, INT targetwidth, INT targetheight)
    {
        return Create(filepath, targetwidth, targetheight, ResolveFormat(filepath));
    }

    static std::shared_ptr<ImgItem> Create(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                           ImgItem::Format imgformat)
    {
        switch (imgformat)
        {
        case ImgItem::Format::JPEG:
            return std::make_shared<ImgJPEGItem>(filepath, targetwidth, targetheight);
        case ImgItem::Format::PNG:
            return std::make_shared<ImgGDIItem>(filepath, targetwidth, targetheight);
        case ImgItem::Format::HEIF:
            return std::make_shared<ImgHEIFItem>(filepath, targetwidth, targetheight);
        case ImgItem::Format::Other:
            return std::make_shared<ImgGDIItem>(filepath, targetwidth, targetheight);
        default:
            throw std::runtime_error("ImgItemFactory::Create(): the specified image format is not supported.");
        }
    }
};

#include "ImageDispatcher.h"

#include "ImgGDIItem.h"
#include "ImgHEIFItem.h"
#include "ImgJPEGItem.h"
#include <stdexcept>

std::shared_ptr<ImgItem> ImageDispatcher::Create(const std::wstring& filepath, INT targetwidth, INT targetheight,
                                                 ImgItem::Format format)
{
    switch (format)
    {
    case ImgItem::Format::JPEG:
        return std::make_shared<ImgJPEGItem>(filepath, targetwidth, targetheight);
    case ImgItem::Format::HEIF:
        return std::make_shared<ImgHEIFItem>(filepath, targetwidth, targetheight);
    case ImgItem::Format::PNG:
    case ImgItem::Format::Other:
        return std::make_shared<ImgGDIItem>(filepath, targetwidth, targetheight);
    default:
        throw std::runtime_error("ImageDispatcher::Create(): the specified image format is not supported.");
    }
}

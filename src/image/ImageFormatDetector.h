#pragma once

#include "ImgItem.h"
#include <Windows.h>
#include <cstddef>

enum class DetectedImageFormat
{
    Unknown,
    JPEG,
    PNG,
    GIF,
    BMP,
    TIFF,
    ICO,
    HEIF
};

class ImageFormatDetector
{
  public:
    static DetectedImageFormat Detect(const BYTE* bytes, std::size_t byte_count);
    static ImgItem::Format ToImgItemFormat(DetectedImageFormat format);
};

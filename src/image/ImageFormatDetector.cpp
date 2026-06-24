#include "ImageFormatDetector.h"

#include <cstring>

namespace
{
bool StartsWith(const BYTE* bytes, std::size_t byte_count, const BYTE* signature, std::size_t signature_size)
{
    return byte_count >= signature_size && std::memcmp(bytes, signature, signature_size) == 0;
}

bool IsHeifBrand(const BYTE* brand)
{
    static const BYTE brands[][4] = {
        {'h', 'e', 'i', 'c'}, {'h', 'e', 'i', 'x'}, {'h', 'e', 'v', 'c'}, {'h', 'e', 'v', 'x'}, {'h', 'e', 'i', 'm'},
        {'h', 'e', 'i', 's'}, {'h', 'e', 'v', 'm'}, {'h', 'e', 'v', 's'}, {'m', 'i', 'f', '1'}, {'m', 's', 'f', '1'}};

    for (const auto& candidate : brands)
    {
        if (std::memcmp(brand, candidate, sizeof(candidate)) == 0)
        {
            return true;
        }
    }

    return false;
}

bool HasHeifBrand(const BYTE* bytes, std::size_t byte_count)
{
    if (byte_count < 12 || std::memcmp(bytes + 4, "ftyp", 4) != 0)
    {
        return false;
    }

    const auto box_size = (static_cast<std::size_t>(bytes[0]) << 24) | (static_cast<std::size_t>(bytes[1]) << 16) |
                          (static_cast<std::size_t>(bytes[2]) << 8) | static_cast<std::size_t>(bytes[3]);
    if (box_size < 12)
    {
        return false;
    }

    const auto bounded_size = box_size < byte_count ? box_size : byte_count;
    if (IsHeifBrand(bytes + 8))
    {
        return true;
    }

    for (std::size_t offset = 16; offset + 4 <= bounded_size; offset += 4)
    {
        if (IsHeifBrand(bytes + offset))
        {
            return true;
        }
    }

    return false;
}
} // namespace

DetectedImageFormat ImageFormatDetector::Detect(const BYTE* bytes, std::size_t byte_count)
{
    if (bytes == nullptr || byte_count == 0)
    {
        return DetectedImageFormat::Unknown;
    }

    const BYTE jpeg_signature[] = {0xFF, 0xD8, 0xFF};
    if (StartsWith(bytes, byte_count, jpeg_signature, sizeof(jpeg_signature)))
    {
        return DetectedImageFormat::JPEG;
    }

    const BYTE png_signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (StartsWith(bytes, byte_count, png_signature, sizeof(png_signature)))
    {
        return DetectedImageFormat::PNG;
    }

    if (StartsWith(bytes, byte_count, reinterpret_cast<const BYTE*>("GIF87a"), 6) ||
        StartsWith(bytes, byte_count, reinterpret_cast<const BYTE*>("GIF89a"), 6))
    {
        return DetectedImageFormat::GIF;
    }

    if (byte_count >= 14 && bytes[0] == 'B' && bytes[1] == 'M')
    {
        return DetectedImageFormat::BMP;
    }

    const BYTE tiff_le_signature[] = {'I', 'I', 0x2A, 0x00};
    const BYTE tiff_be_signature[] = {'M', 'M', 0x00, 0x2A};
    const BYTE big_tiff_le_signature[] = {'I', 'I', 0x2B, 0x00};
    const BYTE big_tiff_be_signature[] = {'M', 'M', 0x00, 0x2B};
    if (StartsWith(bytes, byte_count, tiff_le_signature, sizeof(tiff_le_signature)) ||
        StartsWith(bytes, byte_count, tiff_be_signature, sizeof(tiff_be_signature)) ||
        StartsWith(bytes, byte_count, big_tiff_le_signature, sizeof(big_tiff_le_signature)) ||
        StartsWith(bytes, byte_count, big_tiff_be_signature, sizeof(big_tiff_be_signature)))
    {
        return DetectedImageFormat::TIFF;
    }

    if (byte_count >= 6 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 1 && bytes[3] == 0 &&
        (bytes[4] != 0 || bytes[5] != 0))
    {
        return DetectedImageFormat::ICO;
    }

    if (HasHeifBrand(bytes, byte_count))
    {
        return DetectedImageFormat::HEIF;
    }

    return DetectedImageFormat::Unknown;
}

ImgItem::Format ImageFormatDetector::ToImgItemFormat(DetectedImageFormat format)
{
    switch (format)
    {
    case DetectedImageFormat::JPEG:
        return ImgItem::Format::JPEG;
    case DetectedImageFormat::PNG:
        return ImgItem::Format::PNG;
    case DetectedImageFormat::HEIF:
        return ImgItem::Format::HEIF;
    case DetectedImageFormat::GIF:
    case DetectedImageFormat::BMP:
    case DetectedImageFormat::TIFF:
    case DetectedImageFormat::ICO:
        return ImgItem::Format::Other;
    default:
        return ImgItem::Format::Unsupported;
    }
}

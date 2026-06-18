#include "ExifOrientation.h"

#include <cstdint>

namespace
{
constexpr std::size_t kExifHeaderSize = 6;
constexpr std::size_t kTiffHeaderSize = 8;
constexpr std::size_t kIfdEntrySize = 12;
constexpr std::uint16_t kTiffMagic = 42;
constexpr std::uint16_t kOrientationTag = 0x0112;
constexpr std::uint16_t kShortType = 3;
constexpr std::uint32_t kOrientationCount = 1;

enum class ByteOrder : std::uint8_t
{
    LittleEndian,
    BigEndian
};

bool HasBytes(std::size_t size, std::size_t offset, std::size_t count)
{
    return offset <= size && count <= size - offset;
}

std::uint16_t ReadUint16(const unsigned char* data, ByteOrder byte_order)
{
    if (byte_order == ByteOrder::LittleEndian)
    {
        return static_cast<std::uint16_t>(data[0]) | (static_cast<std::uint16_t>(data[1]) << 8);
    }

    return (static_cast<std::uint16_t>(data[0]) << 8) | static_cast<std::uint16_t>(data[1]);
}

std::uint32_t ReadUint32(const unsigned char* data, ByteOrder byte_order)
{
    if (byte_order == ByteOrder::LittleEndian)
    {
        return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
               (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
    }

    return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}
} // namespace

unsigned int exif::GetOrientation(const unsigned char* data, std::size_t size)
{
    static const unsigned char kExifSignature[kExifHeaderSize] = {'E', 'x', 'i', 'f', 0, 0};

    if (data == nullptr || !HasBytes(size, 0, kExifHeaderSize + kTiffHeaderSize))
    {
        return 0;
    }

    for (std::size_t index = 0; index < kExifHeaderSize; ++index)
    {
        if (data[index] != kExifSignature[index])
        {
            return 0;
        }
    }

    const unsigned char* tiff_data = data + kExifHeaderSize;
    const std::size_t tiff_size = size - kExifHeaderSize;
    ByteOrder byte_order;
    if (tiff_data[0] == 'I' && tiff_data[1] == 'I')
    {
        byte_order = ByteOrder::LittleEndian;
    }
    else if (tiff_data[0] == 'M' && tiff_data[1] == 'M')
    {
        byte_order = ByteOrder::BigEndian;
    }
    else
    {
        return 0;
    }

    if (ReadUint16(tiff_data + 2, byte_order) != kTiffMagic)
    {
        return 0;
    }

    const std::size_t ifd_offset = ReadUint32(tiff_data + 4, byte_order);
    if (!HasBytes(tiff_size, ifd_offset, sizeof(std::uint16_t)))
    {
        return 0;
    }

    const std::uint16_t entry_count = ReadUint16(tiff_data + ifd_offset, byte_order);
    const std::size_t entries_offset = ifd_offset + sizeof(std::uint16_t);
    if (!HasBytes(tiff_size, entries_offset, static_cast<std::size_t>(entry_count) * kIfdEntrySize))
    {
        return 0;
    }

    for (std::uint16_t index = 0; index < entry_count; ++index)
    {
        const unsigned char* entry = tiff_data + entries_offset + static_cast<std::size_t>(index) * kIfdEntrySize;
        if (ReadUint16(entry, byte_order) != kOrientationTag)
        {
            continue;
        }

        if (ReadUint16(entry + 2, byte_order) != kShortType || ReadUint32(entry + 4, byte_order) != kOrientationCount)
        {
            return 0;
        }

        const unsigned int orientation = ReadUint16(entry + 8, byte_order);
        return orientation >= 1 && orientation <= 8 ? orientation : 0;
    }

    return 0;
}

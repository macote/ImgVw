#pragma once

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace OverlayText
{
inline std::wstring FormatByteSize(unsigned long long bytes)
{
    static const wchar_t* kUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    auto value = static_cast<double>(bytes);
    std::size_t unitindex{};
    while (value >= 1024.0 && unitindex < _countof(kUnits) - 1)
    {
        value /= 1024.0;
        ++unitindex;
    }

    std::wstringstream text;
    if (unitindex == 0)
    {
        text << bytes << L" " << kUnits[unitindex];
    }
    else if (value >= 100.0)
    {
        text << static_cast<unsigned long long>(value + 0.5) << L" " << kUnits[unitindex];
    }
    else
    {
        text.setf(std::ios::fixed);
        text.precision(1);
        text << value << L" " << kUnits[unitindex];
    }

    return text.str();
}

inline void WriteByteSizeColumn(std::wostream& text, unsigned long long bytes, std::streamsize width)
{
    constexpr std::streamsize kUnitWidth = 2;
    const auto formatted = FormatByteSize(bytes);
    const auto separator = formatted.rfind(L' ');
    const auto value = formatted.substr(0, separator);
    const auto unit = formatted.substr(separator + 1);
    const auto valuewidth = width - kUnitWidth - 1;

    text << std::right << std::setw(valuewidth) << value << L' ' << std::left << std::setw(kUnitWidth) << unit
         << std::right;
}

inline std::wstring FormatPercent(std::size_t numerator, std::size_t denominator)
{
    std::wstringstream text;
    text << (denominator > 0 ? numerator * 100 / denominator : 0) << L"%";
    return text.str();
}

inline std::wstring BuildItemInfo(const std::wstring& filepath, bool ready, bool loading, int loading_percent,
                                  int image_width, int image_height, bool has_file_size, unsigned long long file_size)
{
    std::wstringstream text;
    if (!ready)
    {
        text << L"[" << (loading ? loading_percent : 0) << L"%] ";
    }
    text << filepath;
    if (ready && ((image_width > 0 && image_height > 0) || has_file_size))
    {
        text << L"\r\n";
        if (image_width > 0 && image_height > 0)
        {
            text << image_width << L" x " << image_height << L" px";
            if (has_file_size)
            {
                text << L"; ";
            }
        }
        if (has_file_size)
        {
            text << FormatByteSize(file_size);
        }
    }
    return text.str();
}
} // namespace OverlayText

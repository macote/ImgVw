#pragma once

#include "UiLanguage.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

namespace OverlayText
{
constexpr wchar_t kNonbreakingSpace = L'\u00A0';
constexpr wchar_t kFrenchPercentSuffix[] = L"\u00A0%";

inline std::wstring FormatByteSize(unsigned long long bytes, UiLanguage language = UiLanguage::English)
{
    static const wchar_t* kEnglishUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    static const wchar_t* kFrenchUnits[] = {L"o", L"Ko", L"Mo", L"Go", L"To"};
    const auto* units = language == UiLanguage::French ? kFrenchUnits : kEnglishUnits;
    const auto unit_space = language == UiLanguage::French ? kNonbreakingSpace : L' ';
    constexpr std::size_t kUnitCount = 5;

    auto value = static_cast<double>(bytes);
    std::size_t unitindex{};
    while (value >= 1024.0 && unitindex < kUnitCount - 1)
    {
        value /= 1024.0;
        ++unitindex;
    }

    std::wstringstream text;
    if (unitindex == 0)
    {
        text << bytes << unit_space << units[unitindex];
    }
    else if (value >= 100.0)
    {
        text << static_cast<unsigned long long>(value + 0.5) << unit_space << units[unitindex];
    }
    else
    {
        text.setf(std::ios::fixed);
        text.precision(1);
        text << value << unit_space << units[unitindex];
    }

    auto formatted = text.str();
    if (language == UiLanguage::French)
    {
        std::replace(formatted.begin(), formatted.end(), L'.', L',');
    }
    return formatted;
}

inline void WriteByteSizeColumn(std::wostream& text, unsigned long long bytes, std::streamsize width,
                                UiLanguage language = UiLanguage::English)
{
    constexpr std::streamsize kUnitWidth = 2;
    const auto formatted = FormatByteSize(bytes, language);
    const auto unit_space = language == UiLanguage::French ? kNonbreakingSpace : L' ';
    const auto separator = formatted.rfind(unit_space);
    const auto value = formatted.substr(0, separator);
    const auto unit = formatted.substr(separator + 1);
    const auto valuewidth = width - kUnitWidth - 1;

    text << std::right << std::setw(valuewidth) << value << unit_space << std::left << std::setw(kUnitWidth) << unit
         << std::right;
}

inline std::wstring FormatPercent(std::size_t numerator, std::size_t denominator,
                                  UiLanguage language = UiLanguage::English)
{
    std::wstringstream text;
    text << (denominator > 0 ? numerator * 100 / denominator : 0);
    text << (language == UiLanguage::French ? kFrenchPercentSuffix : L"%");
    return text.str();
}

inline std::wstring BuildItemInfo(const std::wstring& filepath, bool ready, bool loading, int loading_percent,
                                  int image_width, int image_height, bool has_file_size, unsigned long long file_size,
                                  UiLanguage language = UiLanguage::English)
{
    std::wstringstream text;
    if (!ready)
    {
        text << L"[" << (loading ? loading_percent : 0) << (language == UiLanguage::French ? kFrenchPercentSuffix : L"%")
             << L"] ";
    }
    text << filepath;
    if (ready && ((image_width > 0 && image_height > 0) || has_file_size))
    {
        text << L"\r\n";
        if (image_width > 0 && image_height > 0)
        {
            text << image_width << L" x " << image_height
                 << (language == UiLanguage::French ? kNonbreakingSpace : L' ') << L"px";
            if (has_file_size)
            {
                text << L"; ";
            }
        }
        if (has_file_size)
        {
            text << FormatByteSize(file_size, language);
        }
    }
    return text.str();
}
} // namespace OverlayText

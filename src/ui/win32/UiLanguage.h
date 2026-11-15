#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

enum class UiLanguagePreference
{
    System,
    English,
    French
};

enum class UiLanguage
{
    English,
    French
};

inline constexpr std::uint16_t kEnglishResourceLanguageId = 0x0409;
inline constexpr std::uint16_t kFrenchResourceLanguageId = 0x0C0C;

inline UiLanguage ResolveUiLanguage(UiLanguagePreference preference, std::uint16_t system_ui_language)
{
    if (preference == UiLanguagePreference::English)
    {
        return UiLanguage::English;
    }
    if (preference == UiLanguagePreference::French)
    {
        return UiLanguage::French;
    }

    constexpr std::uint16_t kPrimaryLanguageMask = 0x03FF;
    constexpr std::uint16_t kPrimaryLanguageFrench = 0x000C;
    if ((system_ui_language & kPrimaryLanguageMask) == kPrimaryLanguageFrench)
    {
        return UiLanguage::French;
    }

    return UiLanguage::English;
}

inline UiLanguagePreference ParseUiLanguagePreference(std::wstring_view value)
{
    auto equals_ascii_nocase = [](std::wstring_view lhs, std::wstring_view rhs) {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < lhs.size(); ++i)
        {
            const auto ch = lhs[i];
            if (ch > 0x7F)
            {
                return false;
            }
            if (static_cast<wchar_t>(std::tolower(static_cast<unsigned char>(ch))) != rhs[i])
            {
                return false;
            }
        }
        return true;
    };

    if (equals_ascii_nocase(value, L"en"))
    {
        return UiLanguagePreference::English;
    }
    if (equals_ascii_nocase(value, L"fr"))
    {
        return UiLanguagePreference::French;
    }
    return UiLanguagePreference::System;
}

inline const wchar_t* SerializeUiLanguagePreference(UiLanguagePreference preference)
{
    switch (preference)
    {
    case UiLanguagePreference::English:
        return L"en";
    case UiLanguagePreference::French:
        return L"fr";
    case UiLanguagePreference::System:
    default:
        return L"system";
    }
}

inline std::uint16_t ResourceLanguageId(UiLanguage language)
{
    return language == UiLanguage::French ? kFrenchResourceLanguageId : kEnglishResourceLanguageId;
}

inline std::wstring FormatUiTemplate(std::wstring_view templ, std::initializer_list<std::wstring_view> args)
{
    std::wstring result;
    result.reserve(templ.size());

    for (std::size_t i = 0; i < templ.size(); ++i)
    {
        if (templ[i] == L'{' && i + 1 < templ.size())
        {
            if (templ[i + 1] == L'{')
            {
                result.push_back(L'{');
                ++i;
                continue;
            }

            std::size_t close = i + 1;
            std::size_t index = 0;
            bool valid = false;
            while (close < templ.size() && templ[close] >= L'0' && templ[close] <= L'9')
            {
                valid = true;
                index = index * 10 + static_cast<std::size_t>(templ[close] - L'0');
                ++close;
            }
            if (valid && close < templ.size() && templ[close] == L'}' && index < args.size())
            {
                const auto arg = *(args.begin() + index);
                result.append(arg.data(), arg.size());
                i = close;
                continue;
            }
        }
        else if (templ[i] == L'}' && i + 1 < templ.size() && templ[i + 1] == L'}')
        {
            result.push_back(L'}');
            ++i;
            continue;
        }

        result.push_back(templ[i]);
    }

    return result;
}

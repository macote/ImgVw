#pragma once

#include "RegistryKey.h"
#include "UiLanguage.h"

#include <Windows.h>
#include <cstdint>
#include <string>

struct UiLanguageSettingsOperations
{
    using GetUserDefaultUILanguageFunction = LANGID(WINAPI*)();
    using RegOpenKeyExFunction = LSTATUS(WINAPI*)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
    using RegCreateKeyExFunction =
        LSTATUS(WINAPI*)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
    using RegQueryValueExFunction = LSTATUS(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
    using RegSetValueExFunction = LSTATUS(WINAPI*)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD);
    using RegCloseKeyFunction = LSTATUS(WINAPI*)(HKEY);

    GetUserDefaultUILanguageFunction get_user_default_ui_language{GetUserDefaultUILanguage};
    RegOpenKeyExFunction reg_open_key{RegOpenKeyExW};
    RegCreateKeyExFunction reg_create_key{RegCreateKeyExW};
    RegQueryValueExFunction reg_query_value{RegQueryValueExW};
    RegSetValueExFunction reg_set_value{RegSetValueExW};
    RegCloseKeyFunction reg_close_key{RegCloseKey};
};

namespace UiLanguageSettings
{
inline constexpr wchar_t kRegistrySubKey[] = L"Software\\ImgVw";
inline constexpr wchar_t kRegistryValueName[] = L"Language";

inline std::uint16_t DetectSystemUiLanguage(const UiLanguageSettingsOperations& operations = {})
{
    if (operations.get_user_default_ui_language == nullptr)
    {
        return 0;
    }

    return static_cast<std::uint16_t>(operations.get_user_default_ui_language());
}

inline UiLanguagePreference LoadLanguagePreference(const UiLanguageSettingsOperations& operations = {})
{
    if (operations.reg_open_key == nullptr || operations.reg_query_value == nullptr ||
        operations.reg_close_key == nullptr)
    {
        return UiLanguagePreference::System;
    }

    HKEY raw_key{};
    if (operations.reg_open_key(HKEY_CURRENT_USER, kRegistrySubKey, 0, KEY_READ, &raw_key) != ERROR_SUCCESS ||
        raw_key == nullptr)
    {
        return UiLanguagePreference::System;
    }

    struct KeyCloser
    {
        HKEY key;
        UiLanguageSettingsOperations::RegCloseKeyFunction close_fn;
        ~KeyCloser()
        {
            if (key != nullptr && close_fn != nullptr)
            {
                close_fn(key);
            }
        }
    } closer{raw_key, operations.reg_close_key};

    DWORD type{};
    wchar_t buffer[32]{};
    DWORD bytes = sizeof(buffer);
    const auto query_result = operations.reg_query_value(raw_key, kRegistryValueName, nullptr, &type,
                                                         reinterpret_cast<LPBYTE>(buffer), &bytes);
    if (query_result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof(buffer))
    {
        return UiLanguagePreference::System;
    }

    const auto wchar_count = bytes / sizeof(wchar_t);
    std::wstring value(buffer, wchar_count);
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    return ParseUiLanguagePreference(value);
}

inline bool SaveLanguagePreference(UiLanguagePreference preference,
                                   const UiLanguageSettingsOperations& operations = {})
{
    if (operations.reg_create_key == nullptr || operations.reg_set_value == nullptr ||
        operations.reg_close_key == nullptr)
    {
        return false;
    }

    HKEY raw_key{};
    if (operations.reg_create_key(HKEY_CURRENT_USER, kRegistrySubKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                  nullptr, &raw_key, nullptr) != ERROR_SUCCESS ||
        raw_key == nullptr)
    {
        return false;
    }

    struct KeyCloser
    {
        HKEY key;
        UiLanguageSettingsOperations::RegCloseKeyFunction close_fn;
        ~KeyCloser()
        {
            if (key != nullptr && close_fn != nullptr)
            {
                close_fn(key);
            }
        }
    } closer{raw_key, operations.reg_close_key};

    const std::wstring serialized = SerializeUiLanguagePreference(preference);
    const auto bytes = static_cast<DWORD>((serialized.size() + 1) * sizeof(wchar_t));
    return operations.reg_set_value(raw_key, kRegistryValueName, 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(serialized.c_str()), bytes) == ERROR_SUCCESS;
}
} // namespace UiLanguageSettings

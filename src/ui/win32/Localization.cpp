#include "Localization.h"

#include <cstring>

namespace
{
HINSTANCE g_resource_instance = nullptr;
UiLanguagePreference g_preference = UiLanguagePreference::System;
UiLanguage g_language = UiLanguage::English;

BOOL CALLBACK MatchResourceLanguage(HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR parameter)
{
    auto& match = *reinterpret_cast<WORD*>(parameter);
    if (language == match)
    {
        match = 0;
        return FALSE;
    }
    return TRUE;
}

bool TryReadStringFromResource(HINSTANCE instance, UINT id, std::uint16_t lang_id, std::wstring& out_text)
{
    if (instance == nullptr)
    {
        return false;
    }

    const auto block_id = static_cast<WORD>((id >> 4) + 1);
    const auto index_in_block = static_cast<UINT>(id & 0x0F);

    const auto resource = FindResourceExW(instance, RT_STRING, MAKEINTRESOURCEW(block_id), lang_id);
    if (resource == nullptr)
    {
        return false;
    }

    const auto byte_size = SizeofResource(instance, resource);
    const auto loaded = LoadResource(instance, resource);
    const auto raw_data = loaded == nullptr ? nullptr : LockResource(loaded);
    if (byte_size < sizeof(WORD) || raw_data == nullptr)
    {
        return false;
    }

    const auto* cursor = static_cast<const std::uint8_t*>(raw_data);
    const auto* end = cursor + byte_size;

    for (UINT i = 0; i <= index_in_block; ++i)
    {
        if (cursor + sizeof(WORD) > end)
        {
            return false;
        }

        WORD char_count{};
        std::memcpy(&char_count, cursor, sizeof(WORD));
        cursor += sizeof(WORD);

        const auto string_bytes = static_cast<std::size_t>(char_count) * sizeof(wchar_t);
        if (static_cast<std::size_t>(end - cursor) < string_bytes)
        {
            return false;
        }

        if (i == index_in_block)
        {
            if (char_count == 0)
            {
                return false;
            }
            out_text.assign(reinterpret_cast<const wchar_t*>(cursor), char_count);
            return true;
        }

        cursor += string_bytes;
    }

    return false;
}

HRSRC FindResourceWithEnglishFallback(HINSTANCE instance, LPCWSTR type, LPCWSTR name, UiLanguage language)
{
    if (instance == nullptr)
    {
        return nullptr;
    }

    const auto primary_lang = ResourceLanguageId(language);
    if (const auto resource = FindResourceExW(instance, type, name, primary_lang); resource != nullptr)
    {
        return resource;
    }

    if (primary_lang != kEnglishResourceLanguageId)
    {
        return FindResourceExW(instance, type, name, kEnglishResourceLanguageId);
    }

    return nullptr;
}
} // namespace

namespace Localization
{
void Initialize(HINSTANCE instance, UiLanguagePreference preference, UiLanguage language)
{
    g_resource_instance = instance;
    g_preference = preference;
    g_language = language;
}

void SetLanguage(UiLanguagePreference preference, UiLanguage language)
{
    g_preference = preference;
    g_language = language;
}

HINSTANCE GetInstance()
{
    return g_resource_instance;
}

UiLanguagePreference GetPreference()
{
    return g_preference;
}

UiLanguage GetLanguage()
{
    return g_language;
}

bool TryLoadString(HINSTANCE instance, UINT id, UiLanguage language, std::wstring& out_text)
{
    const auto primary_lang = ResourceLanguageId(language);
    if (TryReadStringFromResource(instance, id, primary_lang, out_text))
    {
        return true;
    }

    if (primary_lang != kEnglishResourceLanguageId &&
        TryReadStringFromResource(instance, id, kEnglishResourceLanguageId, out_text))
    {
        return true;
    }

    return false;
}

bool TryLoadStringExact(HINSTANCE instance, UINT id, UiLanguage language, std::wstring& out_text)
{
    if (instance == nullptr)
    {
        return false;
    }

    const auto language_id = ResourceLanguageId(language);
    auto unmatched_language = language_id;
    const auto block = MAKEINTRESOURCEW((id >> 4) + 1);
    EnumResourceLanguagesW(instance, RT_STRING, block, MatchResourceLanguage,
                           reinterpret_cast<LONG_PTR>(&unmatched_language));
    return unmatched_language == 0 && TryReadStringFromResource(instance, id, language_id, out_text);
}

std::wstring LoadStringForLanguage(HINSTANCE instance, UINT id, UiLanguage language)
{
    std::wstring text;
    if (TryLoadString(instance, id, language, text))
    {
        return text;
    }

    return L"[missing #" + std::to_wstring(id) + L"]";
}

std::wstring GetString(UINT id)
{
    return LoadStringForLanguage(g_resource_instance, id, g_language);
}

std::wstring FormatString(UINT id, std::initializer_list<std::wstring_view> args)
{
    return FormatUiTemplate(GetString(id), args);
}

HMENU LoadMenuForLanguage(HINSTANCE instance, LPCWSTR menu_name, UiLanguage language)
{
    const auto resource = FindResourceWithEnglishFallback(instance, RT_MENU, menu_name, language);
    if (resource == nullptr)
    {
        return nullptr;
    }

    const auto loaded = LoadResource(instance, resource);
    const auto data = loaded == nullptr ? nullptr : LockResource(loaded);
    if (data == nullptr)
    {
        return nullptr;
    }

    return LoadMenuIndirectW(data);
}

HMENU LoadPopupMenu(LPCWSTR menu_name)
{
    return LoadMenuForLanguage(g_resource_instance, menu_name, g_language);
}

INT_PTR ShowDialogForLanguage(HINSTANCE instance, UINT dialog_id, HWND parent, DLGPROC dialog_proc, LPARAM init_param,
                              UiLanguage language)
{
    const auto resource = FindResourceWithEnglishFallback(instance, RT_DIALOG, MAKEINTRESOURCEW(dialog_id), language);
    if (resource == nullptr)
    {
        return -1;
    }

    const auto loaded = LoadResource(instance, resource);
    const auto data = loaded == nullptr ? nullptr : LockResource(loaded);
    if (data == nullptr)
    {
        return -1;
    }

    return DialogBoxIndirectParamW(instance, reinterpret_cast<LPCDLGTEMPLATEW>(data), parent, dialog_proc, init_param);
}

INT_PTR RunModalDialog(UINT dialog_id, HWND parent, DLGPROC dialog_proc, LPARAM init_param)
{
    return ShowDialogForLanguage(g_resource_instance, dialog_id, parent, dialog_proc, init_param, g_language);
}
} // namespace Localization

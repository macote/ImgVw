#include "Localization.h"
#include "UiLanguage.h"
#include "UiLanguageSettings.h"
#include "resource.h"
#include "../support/TestHarness.h"

#include <Windows.h>
#include <cstring>
#include <set>
#include <string>

namespace
{
constexpr UINT kTestEnglishOnlyStringId = 2000;

std::set<std::size_t> ExtractPlaceholders(const std::wstring& text)
{
    std::set<std::size_t> placeholders;
    for (std::size_t i = 0; i + 2 < text.size(); ++i)
    {
        if (text[i] == L'{')
        {
            if (text[i + 1] == L'{')
            {
                ++i;
                continue;
            }
            std::size_t close = i + 1;
            std::size_t index = 0;
            bool valid = false;
            while (close < text.size() && text[close] >= L'0' && text[close] <= L'9')
            {
                valid = true;
                index = index * 10 + static_cast<std::size_t>(text[close] - L'0');
                ++close;
            }
            if (valid && close < text.size() && text[close] == L'}')
            {
                placeholders.insert(index);
                i = close;
            }
        }
    }
    return placeholders;
}

struct FakeRegistryState
{
    LSTATUS open_status{ERROR_SUCCESS};
    LSTATUS query_status{ERROR_SUCCESS};
    LSTATUS create_status{ERROR_SUCCESS};
    LSTATUS set_status{ERROR_SUCCESS};
    DWORD value_type{REG_SZ};
    std::wstring stored_value;
    bool open_called{false};
    bool close_called{false};
};

FakeRegistryState* g_fake_registry = nullptr;
HKEY const kFakeRegistryHandle = reinterpret_cast<HKEY>(static_cast<INT_PTR>(0x1234));

LANGID WINAPI FakeGetUserDefaultUILanguage()
{
    return 0x0C0C;
}

LSTATUS WINAPI FakeRegOpenKeyEx(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY result)
{
    g_fake_registry->open_called = true;
    if (g_fake_registry->open_status != ERROR_SUCCESS)
    {
        *result = nullptr;
        return g_fake_registry->open_status;
    }
    *result = kFakeRegistryHandle;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI FakeRegCreateKeyEx(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES,
                                  PHKEY result, LPDWORD)
{
    if (g_fake_registry->create_status != ERROR_SUCCESS)
    {
        *result = nullptr;
        return g_fake_registry->create_status;
    }
    *result = kFakeRegistryHandle;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI FakeRegQueryValueEx(HKEY, LPCWSTR, LPDWORD, LPDWORD type, LPBYTE data, LPDWORD bytes)
{
    if (g_fake_registry->query_status != ERROR_SUCCESS)
    {
        return g_fake_registry->query_status;
    }
    *type = g_fake_registry->value_type;
    const auto required_bytes = static_cast<DWORD>((g_fake_registry->stored_value.size() + 1) * sizeof(wchar_t));
    if (*bytes < required_bytes)
    {
        *bytes = required_bytes;
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, g_fake_registry->stored_value.c_str(), required_bytes);
    *bytes = required_bytes;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI FakeRegSetValueEx(HKEY, LPCWSTR, DWORD, DWORD type, const BYTE* data, DWORD bytes)
{
    if (g_fake_registry->set_status != ERROR_SUCCESS)
    {
        return g_fake_registry->set_status;
    }
    if (type == REG_SZ && data != nullptr && bytes >= sizeof(wchar_t))
    {
        g_fake_registry->stored_value.assign(reinterpret_cast<const wchar_t*>(data));
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI FakeRegCloseKey(HKEY key)
{
    if (key == kFakeRegistryHandle)
    {
        g_fake_registry->close_called = true;
    }
    return ERROR_SUCCESS;
}

void TestUiLanguageResolution()
{
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x0409) == UiLanguage::English,
          "system US English resolves to English");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x0809) == UiLanguage::English,
          "system UK English resolves to English");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x040C) == UiLanguage::French,
          "system France French resolves to French");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x0C0C) == UiLanguage::French,
          "system Canadian French resolves to French");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x080C) == UiLanguage::French,
          "system Belgian French resolves to French");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x100C) == UiLanguage::French,
          "system Swiss French resolves to French");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0x0411) == UiLanguage::English,
          "unsupported system language resolves to English");
    Check(ResolveUiLanguage(UiLanguagePreference::System, 0) == UiLanguage::English,
          "failed system language detection falls back to English");

    Check(ResolveUiLanguage(UiLanguagePreference::English, 0x0C0C) == UiLanguage::English,
          "explicit English override ignores French system language");
    Check(ResolveUiLanguage(UiLanguagePreference::French, 0x0409) == UiLanguage::French,
          "explicit French override ignores English system language");

    Check(ParseUiLanguagePreference(L"system") == UiLanguagePreference::System, "parses system preference");
    Check(ParseUiLanguagePreference(L"en") == UiLanguagePreference::English, "parses English preference");
    Check(ParseUiLanguagePreference(L"FR") == UiLanguagePreference::French, "parses case-insensitive French preference");
    Check(ParseUiLanguagePreference(L"") == UiLanguagePreference::System, "empty preference defaults to system");
    Check(ParseUiLanguagePreference(L"es") == UiLanguagePreference::System,
          "unsupported preference defaults to system");
    Check(ParseUiLanguagePreference(L"french") == UiLanguagePreference::System,
          "malformed preference defaults to system");

    Check(FormatUiTemplate(L"Error 0x{0} in {1} ({{ok}})", {L"0005", L"test"}) == L"Error 0x0005 in test ({ok})",
          "template formatter substitutes indexed placeholders and escaped braces");
}

void TestUiLanguageSettingsPersistence()
{
    FakeRegistryState state;
    g_fake_registry = &state;

    UiLanguageSettingsOperations ops;
    ops.get_user_default_ui_language = FakeGetUserDefaultUILanguage;
    ops.reg_open_key = FakeRegOpenKeyEx;
    ops.reg_create_key = FakeRegCreateKeyEx;
    ops.reg_query_value = FakeRegQueryValueEx;
    ops.reg_set_value = FakeRegSetValueEx;
    ops.reg_close_key = FakeRegCloseKey;

    Check(UiLanguageSettings::DetectSystemUiLanguage(ops) == 0x0C0C, "detects OS UI language via adapter");

    state.open_status = ERROR_FILE_NOT_FOUND;
    Check(UiLanguageSettings::LoadLanguagePreference(ops) == UiLanguagePreference::System,
          "missing registry key defaults to system");

    state.open_status = ERROR_SUCCESS;
    state.stored_value = L"fr";
    Check(UiLanguageSettings::LoadLanguagePreference(ops) == UiLanguagePreference::French && state.close_called,
          "loads saved French preference and closes handle");

    state.stored_value = L"garbage";
    Check(UiLanguageSettings::LoadLanguagePreference(ops) == UiLanguagePreference::System,
          "malformed registry value defaults to system");

    state.value_type = REG_DWORD;
    Check(UiLanguageSettings::LoadLanguagePreference(ops) == UiLanguagePreference::System,
          "wrong registry value type defaults to system");
    state.value_type = REG_SZ;

    state.close_called = false;
    Check(UiLanguageSettings::SaveLanguagePreference(UiLanguagePreference::English, ops) &&
              state.stored_value == L"en" && state.close_called,
          "saves English preference");

    state.set_status = ERROR_ACCESS_DENIED;
    Check(!UiLanguageSettings::SaveLanguagePreference(UiLanguagePreference::French, ops),
          "reports persistence failure without crashing");

    g_fake_registry = nullptr;
}

void TestEmbeddedLanguageResources()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    Check(instance != nullptr, "test executable instance handle is valid");

    for (UINT id = IDS_APP_ERROR_TITLE; id <= IDS_STATS_COL_USED; ++id)
    {
        std::wstring en_text;
        std::wstring fr_text;
        Check(Localization::TryLoadStringExact(instance, id, UiLanguage::English, en_text),
              "English string catalog contains all expected IDS_* entries");
        Check(Localization::TryLoadStringExact(instance, id, UiLanguage::French, fr_text),
              "French string catalog contains all expected IDS_* entries");
        Check(ExtractPlaceholders(en_text) == ExtractPlaceholders(fr_text),
              "English and French templates have matching placeholders");
    }

    const auto fr_about_title = Localization::LoadStringForLanguage(instance, IDS_EMPTY_NO_IMAGES, UiLanguage::French);
    Check(fr_about_title.find(L'\u00E9') != std::wstring::npos,
          "French catalog preserves accented characters (Windows-1252 -> UTF-16)");
    const auto fr_summary = Localization::LoadStringForLanguage(instance, IDS_STATS_SUMMARY, UiLanguage::French);
    Check(fr_summary.find(L"\u00A0:") != std::wstring::npos && fr_summary.find(L" ;") == std::wstring::npos,
          "Canadian French statistics use a nonbreaking space before colons and no space before semicolons");

    std::wstring fallback_text;
    Check(!Localization::TryLoadStringExact(instance, kTestEnglishOnlyStringId, UiLanguage::French, fallback_text),
          "exact lookup rejects an English-only string");
    Check(Localization::TryLoadString(instance, kTestEnglishOnlyStringId, UiLanguage::French, fallback_text) &&
              fallback_text == L"English fallback test",
          "missing French string intentionally falls back to English");

    const auto en_menu = Localization::LoadMenuForLanguage(instance, L"IMGPOPUP", UiLanguage::English);
    const auto fr_menu = Localization::LoadMenuForLanguage(instance, L"IMGPOPUP", UiLanguage::French);
    Check(en_menu != nullptr && fr_menu != nullptr, "loads both English and French popup menus explicitly");

    wchar_t en_open[128]{};
    wchar_t fr_open[128]{};
    GetMenuStringW(en_menu, IDM_OPEN_IMAGE, en_open, _countof(en_open), MF_BYCOMMAND);
    GetMenuStringW(fr_menu, IDM_OPEN_IMAGE, fr_open, _countof(fr_open), MF_BYCOMMAND);
    Check(std::wstring(en_open).find(L"Open image") != std::wstring::npos, "English menu has English item text");
    Check(std::wstring(fr_open).find(L"Ouvrir une image") != std::wstring::npos, "French menu has French item text");
    Check(GetMenuState(fr_menu, IDM_LANGUAGE_SYSTEM, MF_BYCOMMAND) != static_cast<UINT>(-1) &&
              GetMenuState(fr_menu, IDM_LANGUAGE_ENGLISH, MF_BYCOMMAND) != static_cast<UINT>(-1) &&
              GetMenuState(fr_menu, IDM_LANGUAGE_FRENCH, MF_BYCOMMAND) != static_cast<UINT>(-1),
          "menu contains Language submenu command IDs");

    DestroyMenu(en_menu);
    DestroyMenu(fr_menu);
}
} // namespace

void RunLocalizationTests()
{
    TestUiLanguageResolution();
    TestUiLanguageSettingsPersistence();
    TestEmbeddedLanguageResources();
}

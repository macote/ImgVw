#pragma once

#include "UiLanguage.h"

#include <Windows.h>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace Localization
{
void Initialize(HINSTANCE instance, UiLanguagePreference preference, UiLanguage language);
void SetLanguage(UiLanguagePreference preference, UiLanguage language);

HINSTANCE GetInstance();
UiLanguagePreference GetPreference();
UiLanguage GetLanguage();

bool TryLoadString(HINSTANCE instance, UINT id, UiLanguage language, std::wstring& out_text);
bool TryLoadStringExact(HINSTANCE instance, UINT id, UiLanguage language, std::wstring& out_text);
std::wstring LoadStringForLanguage(HINSTANCE instance, UINT id, UiLanguage language);
std::wstring GetString(UINT id);
std::wstring FormatString(UINT id, std::initializer_list<std::wstring_view> args);

HMENU LoadMenuForLanguage(HINSTANCE instance, LPCWSTR menu_name, UiLanguage language);
HMENU LoadPopupMenu(LPCWSTR menu_name);

INT_PTR ShowDialogForLanguage(HINSTANCE instance, UINT dialog_id, HWND parent, DLGPROC dialog_proc, LPARAM init_param,
                              UiLanguage language);
INT_PTR RunModalDialog(UINT dialog_id, HWND parent, DLGPROC dialog_proc, LPARAM init_param = 0);
} // namespace Localization

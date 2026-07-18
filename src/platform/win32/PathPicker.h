#pragma once

#include <Windows.h>
#include <string>

enum class PathPickerStatus
{
    Selected,
    Cancelled,
    Failed,
};

struct PathPickerResult
{
    PathPickerStatus status{PathPickerStatus::Failed};
    std::wstring path;
    HRESULT error{E_FAIL};
};

class PathPicker final
{
  public:
    PathPickerResult SelectImage(HWND owner) const;
    PathPickerResult SelectFolder(HWND owner) const;

  private:
    PathPickerResult SelectImageLegacy(HWND owner) const;
    PathPickerResult SelectFolderLegacy(HWND owner) const;
};

#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

enum class FileDeleteMode
{
    Permanent,
    Recycle
};

enum class FileOperationStatus
{
    Succeeded,
    Cancelled,
    Failed,
    InvalidPath
};

struct FileOperationResult
{
    FileOperationStatus status{FileOperationStatus::Failed};
    int shell_error{};
};

class FileOperations final
{
  public:
    using ShellFileOperation = int(WINAPI*)(LPSHFILEOPSTRUCTW);

    explicit FileOperations(ShellFileOperation shellfileoperation = SHFileOperationW)
        : shell_file_operation_(shellfileoperation)
    {
    }

    FileOperationResult Delete(HWND owner, const std::wstring& filepath, FileDeleteMode mode) const;
    static std::vector<wchar_t> BuildPathList(const std::wstring& filepath);
    static FILEOP_FLAGS DeleteFlags(FileDeleteMode mode);

  private:
    ShellFileOperation shell_file_operation_;
};

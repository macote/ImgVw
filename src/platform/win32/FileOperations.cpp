#include "FileOperations.h"

FileOperationResult FileOperations::Delete(HWND owner, const std::wstring& filepath, FileDeleteMode mode) const
{
    if (filepath.empty())
    {
        return {FileOperationStatus::InvalidPath, 0};
    }

    auto paths = BuildPathList(filepath);
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = owner;
    operation.wFunc = FO_DELETE;
    operation.pFrom = paths.data();
    operation.fFlags = DeleteFlags(mode);

    const auto shellerror = shell_file_operation_(&operation);
    if (operation.fAnyOperationsAborted)
    {
        return {FileOperationStatus::Cancelled, shellerror};
    }

    if (shellerror != 0)
    {
        return {FileOperationStatus::Failed, shellerror};
    }

    return {FileOperationStatus::Succeeded, 0};
}

std::vector<wchar_t> FileOperations::BuildPathList(const std::wstring& filepath)
{
    std::vector<wchar_t> paths(filepath.begin(), filepath.end());
    paths.push_back(L'\0');
    paths.push_back(L'\0');
    return paths;
}

FILEOP_FLAGS FileOperations::DeleteFlags(FileDeleteMode mode)
{
    auto flags = static_cast<FILEOP_FLAGS>(FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR |
                                           FOF_WANTNUKEWARNING);
    if (mode == FileDeleteMode::Recycle)
    {
        flags = static_cast<FILEOP_FLAGS>(flags | FOF_ALLOWUNDO);
    }

    return flags;
}

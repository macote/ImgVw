#include "BrowsePath.h"

#include <Shlwapi.h>

BrowsePathResult ClassifyBrowsePath(const std::wstring& path)
{
    BrowsePathResult result;
    if (path.empty())
    {
        result.win32_error = ERROR_INVALID_PARAMETER;
        return result;
    }

    const auto forced_folder = path.back() == L'\\' || path.back() == L'/';
    auto workpath = path;
    while (workpath.size() > 1 && (workpath.back() == L'\\' || workpath.back() == L'/') &&
           !PathIsRoot(workpath.c_str()))
    {
        workpath.pop_back();
    }

    const auto attributes = GetFileAttributes(workpath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        result.win32_error = GetLastError();
        return result;
    }

    const auto is_folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (forced_folder && !is_folder)
    {
        result.win32_error = ERROR_DIRECTORY;
        return result;
    }

    if (is_folder || forced_folder)
    {
        result.kind = BrowsePathKind::Folder;
        result.folderpath = workpath;
        if (result.folderpath.back() != L'\\' && result.folderpath.back() != L'/')
        {
            result.folderpath += L'\\';
        }
        return result;
    }

    result.kind = BrowsePathKind::File;
    const auto backslashposition = workpath.find_last_of(L"\\/");
    if (backslashposition == std::wstring::npos)
    {
        result.folderpath = L".\\";
        result.filepath = result.folderpath + workpath;
    }
    else
    {
        result.folderpath = workpath.substr(0, backslashposition + 1);
        result.filepath = workpath;
    }

    return result;
}

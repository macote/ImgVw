#include "FolderScanner.h"

#include "FindHandle.h"
#include "ImgFormatResolver.h"

namespace
{
bool Cancelled(const FolderScanCallbacks& callbacks)
{
    return callbacks.cancelled && callbacks.cancelled();
}

void PreserveError(FolderScanResult* result, DWORD error)
{
    if (result->win32_error == ERROR_SUCCESS)
    {
        result->win32_error = error;
    }
}
} // namespace

FolderScanResult FolderScanner::Scan(const std::wstring& folderpath, bool recursive,
                                     const FolderScanCallbacks& callbacks) const
{
    return ScanFolder(folderpath, recursive, callbacks);
}

FolderScanResult FolderScanner::ScanFolders(const std::vector<std::wstring>& folderpaths,
                                            const FolderScanCallbacks& callbacks) const
{
    FolderScanResult result;
    for (const auto& folderpath : folderpaths)
    {
        if (Cancelled(callbacks))
        {
            break;
        }

        const auto folder_result = ScanFolder(folderpath, true, callbacks);
        PreserveError(&result, folder_result.win32_error);
    }
    return result;
}

FolderScanResult FolderScanner::ScanFolder(const std::wstring& folderpath, bool recursive,
                                           const FolderScanCallbacks& callbacks) const
{
    FolderScanResult result;
    WIN32_FIND_DATA findfiledata{};
    const std::wstring pattern = folderpath + L"*";
    FindHandle findhandle(FindFirstFile(pattern.c_str(), &findfiledata));
    if (!findhandle.valid())
    {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND)
        {
            result.win32_error = error;
        }
        return result;
    }

    do
    {
        if (Cancelled(callbacks))
        {
            break;
        }

        if (findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (lstrcmp(findfiledata.cFileName, L".") != 0 && lstrcmp(findfiledata.cFileName, L"..") != 0)
            {
                const std::wstring currentpath = folderpath + findfiledata.cFileName + L"\\";
                if (recursive)
                {
                    const auto nested_result = ScanFolder(currentpath, true, callbacks);
                    PreserveError(&result, nested_result.win32_error);
                }
                else if (callbacks.folder_found)
                {
                    callbacks.folder_found(currentpath);
                }
            }
        }
        else
        {
            const std::wstring currentfile = folderpath + findfiledata.cFileName;
            const auto imgformat = ImgFormatResolver::Resolve(currentfile);
            if (imgformat != ImgItem::Format::Unsupported && callbacks.file_found)
            {
                callbacks.file_found(currentfile, imgformat);
            }
        }
    } while (FindNextFile(findhandle.get(), &findfiledata));

    const auto error = GetLastError();
    if (!Cancelled(callbacks) && error != ERROR_NO_MORE_FILES)
    {
        PreserveError(&result, error);
    }
    return result;
}

#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#if !defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06000000
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif

#include "PathPicker.h"

#include <ShlObj.h>
#include <ShObjIdl.h>
#include <commdlg.h>

namespace
{
constexpr COMDLG_FILTERSPEC kImageFileTypes[] = {
    {L"Supported images", L"*.jpg;*.jpeg;*.png;*.heic;*.heif;*.hif;*.bmp;*.gif;*.ico;*.tif;*.tiff"},
    {L"All files", L"*.*"},
};

constexpr wchar_t kLegacyImageFilter[] =
    L"Supported images\0*.jpg;*.jpeg;*.png;*.heic;*.heif;*.hif;*.bmp;*.gif;*.ico;*.tif;*.tiff\0All files\0*.*\0";

PathPickerResult FailedResult(HRESULT error)
{
    PathPickerResult result;
    result.status = PathPickerStatus::Failed;
    result.error = error;
    return result;
}

PathPickerResult CancelledResult()
{
    PathPickerResult result;
    result.status = PathPickerStatus::Cancelled;
    result.error = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return result;
}

PathPickerResult SelectedResult(PWSTR path)
{
    PathPickerResult result;
    result.status = PathPickerStatus::Selected;
    result.path = path == nullptr ? L"" : path;
    result.error = S_OK;
    return result;
}

INT CALLBACK BrowseForFolderCallback(HWND, UINT, LPARAM, LPARAM)
{
    return 0;
}
} // namespace

PathPickerResult PathPicker::SelectImage(HWND owner) const
{
    IFileOpenDialog* dialog{};
    const auto create_result =
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(create_result))
    {
        return SelectImageLegacy(owner);
    }

    const auto set_types_result = dialog->SetFileTypes(_countof(kImageFileTypes), kImageFileTypes);
    if (FAILED(set_types_result))
    {
        dialog->Release();
        return FailedResult(set_types_result);
    }

    const auto set_type_index_result = dialog->SetFileTypeIndex(1);
    if (FAILED(set_type_index_result))
    {
        dialog->Release();
        return FailedResult(set_type_index_result);
    }

    FILEOPENDIALOGOPTIONS options{};
    const auto get_options_result = dialog->GetOptions(&options);
    if (FAILED(get_options_result))
    {
        dialog->Release();
        return FailedResult(get_options_result);
    }

    const auto set_options_result =
        dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    if (FAILED(set_options_result))
    {
        dialog->Release();
        return FailedResult(set_options_result);
    }

    const auto show_result = dialog->Show(owner);
    if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        dialog->Release();
        return CancelledResult();
    }
    if (FAILED(show_result))
    {
        dialog->Release();
        return FailedResult(show_result);
    }

    IShellItem* item{};
    const auto get_result_result = dialog->GetResult(&item);
    dialog->Release();
    if (FAILED(get_result_result))
    {
        return FailedResult(get_result_result);
    }

    PWSTR path{};
    const auto get_path_result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    if (FAILED(get_path_result))
    {
        return FailedResult(get_path_result);
    }

    const auto result = SelectedResult(path);
    CoTaskMemFree(path);
    return result;
}

PathPickerResult PathPicker::SelectFolder(HWND owner) const
{
    IFileOpenDialog* dialog{};
    const auto create_result =
        CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(create_result))
    {
        return SelectFolderLegacy(owner);
    }

    FILEOPENDIALOGOPTIONS options{};
    const auto get_options_result = dialog->GetOptions(&options);
    if (FAILED(get_options_result))
    {
        dialog->Release();
        return FailedResult(get_options_result);
    }

    const auto set_options_result =
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    if (FAILED(set_options_result))
    {
        dialog->Release();
        return FailedResult(set_options_result);
    }

    const auto show_result = dialog->Show(owner);
    if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        dialog->Release();
        return CancelledResult();
    }
    if (FAILED(show_result))
    {
        dialog->Release();
        return FailedResult(show_result);
    }

    IShellItem* item{};
    const auto get_result_result = dialog->GetResult(&item);
    dialog->Release();
    if (FAILED(get_result_result))
    {
        return FailedResult(get_result_result);
    }

    PWSTR path{};
    const auto get_path_result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    if (FAILED(get_path_result))
    {
        return FailedResult(get_path_result);
    }

    const auto result = SelectedResult(path);
    CoTaskMemFree(path);
    return result;
}

PathPickerResult PathPicker::SelectImageLegacy(HWND owner) const
{
    wchar_t path[MAX_PATH]{};
    OPENFILENAME open_file_name{};
    open_file_name.lStructSize = sizeof(open_file_name);
    open_file_name.hwndOwner = owner;
    open_file_name.lpstrFilter = kLegacyImageFilter;
    open_file_name.lpstrFile = path;
    open_file_name.nMaxFile = _countof(path);
    open_file_name.nFilterIndex = 1;
    open_file_name.lpstrTitle = L"Open image";
    open_file_name.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileName(&open_file_name))
    {
        const auto error = CommDlgExtendedError();
        return error == 0 ? CancelledResult() : FailedResult(HRESULT_FROM_WIN32(error));
    }

    return {PathPickerStatus::Selected, path, S_OK};
}

PathPickerResult PathPicker::SelectFolderLegacy(HWND owner) const
{
    BROWSEINFO browse_info{};
    browse_info.hwndOwner = owner;
    browse_info.lpszTitle = L"Open folder";
    browse_info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    browse_info.lpfn = BrowseForFolderCallback;

    PIDLIST_ABSOLUTE item_id_list = SHBrowseForFolder(&browse_info);
    if (item_id_list == nullptr)
    {
        return CancelledResult();
    }

    wchar_t path[MAX_PATH]{};
    const auto converted = SHGetPathFromIDList(item_id_list, path);
    CoTaskMemFree(item_id_list);
    if (!converted)
    {
        return FailedResult(HRESULT_FROM_WIN32(GetLastError()));
    }

    return {PathPickerStatus::Selected, path, S_OK};
}

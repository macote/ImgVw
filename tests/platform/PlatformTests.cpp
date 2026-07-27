#include "BrowsePath.h"
#include "CompatibleDeviceContext.h"
#include "ComPtr.h"
#include "CriticalSection.h"
#include "FileOperations.h"
#include "FindHandle.h"
#include "GdiObject.h"
#include "GlobalMemory.h"
#include "ImgBitmap.h"
#include "ImgBuffer.h"
#include "ImgSettings.h"
#include "SelectedGdiObject.h"
#include "RegistryKey.h"
#include "Win32Handle.h"
#include "WindowDeviceContext.h"
#include "../support/TempFile.h"
#include "../support/TestHarness.h"

#include <Windows.h>

#include <objidl.h>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
SHFILEOPSTRUCTW captured_file_operation{};
std::vector<wchar_t> captured_paths;
int shell_result = 0;
BOOL shell_aborted = FALSE;

int WINAPI MockShellFileOperation(LPSHFILEOPSTRUCTW operation)
{
    captured_file_operation = *operation;
    captured_paths.clear();
    const auto* current = operation->pFrom;
    do
    {
        captured_paths.push_back(*current);
    } while (*current++ != L'\0' || *current != L'\0');
    captured_paths.push_back(L'\0');
    operation->fAnyOperationsAborted = shell_aborted;
    return shell_result;
}

void TestWin32HandleOwnership()
{
    Win32Handle event(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    Check(event.valid(), "Win32 handle owns a created event");

    const auto first_event = event.get();
    Win32Handle moved(std::move(event));
    Check(!event.valid() && moved.get() == first_event, "Win32 handle move transfers ownership");

    const auto replacement_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    Check(replacement_event != nullptr, "replacement event is created");
    moved.reset(replacement_event);
    SetLastError(ERROR_SUCCESS);
    Check(!SetEvent(first_event) && GetLastError() == ERROR_INVALID_HANDLE,
          "Win32 handle reset closes the previous handle");

    const auto released_event = moved.release();
    Check(!moved.valid() && SetEvent(released_event), "Win32 handle release returns ownership");
    CloseHandle(released_event);
}

void TestFindHandleOwnership()
{
    WIN32_FIND_DATA find_data{};
    FindHandle find(FindFirstFile(L".\\*", &find_data));
    Check(find.valid(), "find handle owns an active enumeration");

    FindHandle moved(std::move(find));
    Check(!find.valid() && moved.valid(), "find handle move transfers ownership");

    const auto released_find = moved.release();
    Check(!moved.valid(), "find handle release clears ownership");
    if (released_find != INVALID_HANDLE_VALUE)
    {
        FindClose(released_find);
    }
}

void TestCriticalSectionOwnership()
{
    CriticalSection critical_section;
    Check(critical_section.valid(), "critical section initializes");

    int guarded_value = 0;
    {
        CriticalSectionLock lock(critical_section);
        ++guarded_value;
    }

    Check(guarded_value == 1, "critical section lock guards a scope");
}

void TestGdiOwnership()
{
    WindowDeviceContext window_dc(nullptr, GetDC(nullptr));
    Check(window_dc.valid(), "window DC owner acquires a screen DC");
    const auto raw_window_dc = window_dc.get();
    WindowDeviceContext moved_window_dc(std::move(window_dc));
    Check(!window_dc.valid() && moved_window_dc.get() == raw_window_dc, "window DC move transfers release ownership");

    CompatibleDeviceContext dc(CreateCompatibleDC(nullptr));
    Check(dc.valid(), "compatible DC owner creates a memory DC");

    const auto raw_dc = dc.get();
    CompatibleDeviceContext moved_dc(std::move(dc));
    Check(!dc.valid() && moved_dc.get() == raw_dc, "compatible DC move transfers ownership");

    GdiObject<HBITMAP> bitmap(CreateCompatibleBitmap(moved_dc.get(), 2, 2));
    Check(bitmap.valid(), "GDI object owner creates a bitmap");
    const auto raw_bitmap = bitmap.get();
    GdiObject<HBITMAP> moved_bitmap(std::move(bitmap));
    Check(!bitmap.valid() && moved_bitmap.get() == raw_bitmap, "GDI object move transfers ownership");

    const auto original_bitmap = GetCurrentObject(moved_dc.get(), OBJ_BITMAP);
    {
        SelectedGdiObject selection(moved_dc.get(), moved_bitmap.get());
        Check(selection.valid(), "selected GDI object guard selects a bitmap");
        Check(GetCurrentObject(moved_dc.get(), OBJ_BITMAP) == moved_bitmap.get(),
              "selected GDI object guard exposes the selected bitmap");

        SelectedGdiObject moved_selection(std::move(selection));
        Check(!selection.valid() && moved_selection.valid(), "selected GDI object move transfers restoration");
    }

    Check(GetCurrentObject(moved_dc.get(), OBJ_BITMAP) == original_bitmap,
          "selected GDI object guard restores the previous bitmap");
}

void TestRegistryAndComOwnership()
{
    RegistryKey current_user;
    Check(RegOpenCurrentUser(KEY_QUERY_VALUE, current_user.put()) == ERROR_SUCCESS && current_user.valid(),
          "registry key owns an opened key");
    const auto raw_key = current_user.get();
    RegistryKey moved_key(std::move(current_user));
    Check(!current_user.valid() && moved_key.get() == raw_key, "registry key move transfers ownership");

    GlobalMemory memory(GlobalAlloc(GMEM_MOVEABLE, 16));
    Check(memory.valid(), "global memory owns an allocation");
    IStream* raw_stream{};
    Check(CreateStreamOnHGlobal(memory.get(), TRUE, &raw_stream) == S_OK, "COM stream adopts global memory");
    if (raw_stream != nullptr)
    {
        memory.release();
    }
    ComPtr<IStream> stream(raw_stream);
    ComPtr<IStream> moved_stream(std::move(stream));
    Check(!stream.valid() && moved_stream.valid(), "COM pointer move transfers interface ownership");

    CoTaskMemPtr<BYTE> task_memory(reinterpret_cast<BYTE*>(CoTaskMemAlloc(16)));
    Check(task_memory.valid(), "COM task-memory pointer owns an allocation");
    CoTaskMemPtr<BYTE> moved_task_memory(std::move(task_memory));
    Check(!task_memory.valid() && moved_task_memory.valid(), "COM task-memory move transfers ownership");
}

void TestImgSettingsTempDirectory()
{
    const auto temp_path = ImgSettings::GetInstance().temppath();
    Check(!temp_path.empty(), "image settings creates a temporary path");
    const auto attributes = GetFileAttributesW(temp_path.c_str());
    Check(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
          "image settings temporary path is an existing directory");
}

void TestBrowsePathClassification()
{
    wchar_t temp_path[MAX_PATH]{};
    Check(GetTempPath(MAX_PATH, temp_path) != 0, "temporary path is available");
    const auto test_folder = std::wstring(temp_path) + L"ImgVwBrowsePath-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectory(test_folder.c_str(), nullptr);
    const auto test_file = test_folder + L"\\sample.jpg";
    Win32Handle file(
        CreateFile(test_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(file.valid(), "browse path test file is created");
    file.reset();

    const auto folder = ClassifyBrowsePath(test_folder);
    Check(folder.Succeeded() && folder.kind == BrowsePathKind::Folder && folder.folderpath.back() == L'\\',
          "folder path is classified with a trailing separator");

    const auto forced_folder = ClassifyBrowsePath(test_folder + L"\\");
    Check(forced_folder.Succeeded() && forced_folder.folderpath == folder.folderpath,
          "forced folder path is normalized without duplicate separators");

    const auto absolute_file = ClassifyBrowsePath(test_file);
    Check(absolute_file.Succeeded() && absolute_file.kind == BrowsePathKind::File &&
              absolute_file.folderpath == folder.folderpath && absolute_file.filepath == test_file,
          "absolute file path is split into its file and folder paths");
    Check(BrowsePathsShareFolder(test_file, test_folder),
          "file and folder paths in the same browsing scope share decoded caches");

    const auto other_folder = test_folder + L"-other";
    CreateDirectory(other_folder.c_str(), nullptr);
    Check(!BrowsePathsShareFolder(test_file, other_folder),
          "paths in different browsing scopes do not share decoded caches");

    const auto file_as_folder = ClassifyBrowsePath(test_file + L"\\");
    Check(!file_as_folder.Succeeded() && file_as_folder.win32_error == ERROR_DIRECTORY,
          "file path with a trailing separator is rejected as a folder");

    wchar_t previous_folder[MAX_PATH]{};
    GetCurrentDirectory(MAX_PATH, previous_folder);
    SetCurrentDirectory(test_folder.c_str());
    const auto relative_file = ClassifyBrowsePath(L"sample.jpg");
    SetCurrentDirectory(previous_folder);
    Check(relative_file.Succeeded() && relative_file.filepath == L".\\sample.jpg" && relative_file.folderpath == L".\\",
          "relative file path retains the browser's relative-path convention");

    const auto missing = ClassifyBrowsePath(test_folder + L"\\missing.jpg");
    Check(!missing.Succeeded() && missing.win32_error != ERROR_SUCCESS, "missing browse path reports a native error");

    DeleteFile(test_file.c_str());
    RemoveDirectory(other_folder.c_str());
    RemoveDirectory(test_folder.c_str());
}

void TestImgBufferValidationAndMapping()
{
    const ImgBufferError write_error(ImgBufferStatus::WriteFailed, ERROR_WRITE_FAULT);
    Check(write_error.status() == ImgBufferStatus::WriteFailed && write_error.win32_error() == ERROR_WRITE_FAULT,
          "image buffer errors preserve the failed operation and native error");

    const BYTE pixels[] = {1, 2, 3, 4, 5, 6, 0, 0};
    ImgBuffer buffer;
    buffer.WriteData(2, 1, 8, pixels);
    Check(buffer.width() == 2 && buffer.height() == 1 && buffer.stride() == 8 && buffer.buffersize() == sizeof(pixels),
          "image buffer records validated geometry");
    const auto mapping = buffer.GetFileMapView();
    Check(mapping.filesize().HighPart == 0 && mapping.filesize().LowPart == sizeof(pixels),
          "image buffer writes the complete expected file");
    Check(std::memcmp(mapping.data(), pixels, sizeof(pixels)) == 0, "image buffer mapping preserves pixel data");

    bool rejected = false;
    try
    {
        ImgBuffer invalid;
        invalid.WriteData(0, 1, 4, pixels);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "image buffer rejects invalid dimensions");

    rejected = false;
    try
    {
        ImgBuffer invalid;
        invalid.WriteData(2, 1, 4, pixels);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "image buffer rejects a short 24-bpp stride");

    rejected = false;
    try
    {
        ImgBuffer invalid;
        invalid.WriteData(1, (std::numeric_limits<INT>::max)(), 4, pixels);
    }
    catch (const std::overflow_error&)
    {
        rejected = true;
    }
    Check(rejected, "image buffer rejects a file size above the Win32 limit");

    BITMAPINFO bitmapinfo{};
    bitmapinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapinfo.bmiHeader.biWidth = 2;
    bitmapinfo.bmiHeader.biHeight = 1;
    bitmapinfo.bmiHeader.biPlanes = 1;
    bitmapinfo.bmiHeader.biBitCount = 24;
    bitmapinfo.bmiHeader.biCompression = BI_RGB;
    rejected = false;
    try
    {
        ImgBitmap invalid(&bitmapinfo, pixels, 4);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "display bitmap rejects a buffer smaller than its geometry");

    const auto missing_path = TempPath(L"imgvw-missing-map.bin");
    DeleteFileW(missing_path.c_str());
    rejected = false;
    try
    {
        FileMapView missing(missing_path, FileMapView::Mode::Read);
    }
    catch (const FileMapError& error)
    {
        rejected = error.status() == FileMapStatus::OpenFailed && error.win32_error() == ERROR_FILE_NOT_FOUND;
    }
    Check(rejected, "file mapping preserves its open status and native error");
}

void TestFileOperationPathList()
{
    const auto paths = FileOperations::BuildPathList(L"C:\\images\\photo.jpg");

    Check(paths.size() == 21, "delete path list includes two terminators");
    Check(paths[paths.size() - 1] == L'\0', "delete path list has final terminator");
    Check(paths[paths.size() - 2] == L'\0', "delete path list is double-null terminated");
    Check(std::wstring(paths.data()) == L"C:\\images\\photo.jpg", "delete path list preserves path");
}

void TestFileOperationFlags()
{
    const auto permanent = FileOperations::DeleteFlags(FileDeleteMode::Permanent);
    const auto recycle = FileOperations::DeleteFlags(FileDeleteMode::Recycle);

    Check((permanent & FOF_ALLOWUNDO) == 0, "permanent delete does not allow undo");
    Check((recycle & FOF_ALLOWUNDO) != 0, "recycle delete allows undo");
    Check((permanent & FOF_NOCONFIRMATION) != 0, "delete suppresses confirmation");
    Check((permanent & FOF_WANTNUKEWARNING) != 0, "delete retains permanent-delete warning behavior");
}

void TestFileOperationResults()
{
    FileOperations operations(MockShellFileOperation);

    shell_result = 0;
    shell_aborted = FALSE;
    auto result = operations.Delete(nullptr, L"C:\\images\\photo.jpg", FileDeleteMode::Recycle);
    Check(result.status == FileOperationStatus::Succeeded, "successful shell delete is reported");
    Check(captured_file_operation.wFunc == FO_DELETE, "shell operation uses delete");
    Check((captured_file_operation.fFlags & FOF_ALLOWUNDO) != 0, "recycle mode reaches shell operation");
    Check(captured_paths[captured_paths.size() - 1] == L'\0' && captured_paths[captured_paths.size() - 2] == L'\0',
          "shell operation receives double-null path list");

    shell_result = 5;
    shell_aborted = FALSE;
    result = operations.Delete(nullptr, L"C:\\images\\photo.jpg", FileDeleteMode::Permanent);
    Check(result.status == FileOperationStatus::Failed, "shell error is reported");
    Check(result.shell_error == 5, "shell error code is preserved");

    shell_result = 0;
    shell_aborted = TRUE;
    result = operations.Delete(nullptr, L"C:\\images\\photo.jpg", FileDeleteMode::Permanent);
    Check(result.status == FileOperationStatus::Cancelled, "aborted shell operation is reported");

    result = operations.Delete(nullptr, L"", FileDeleteMode::Permanent);
    Check(result.status == FileOperationStatus::InvalidPath, "empty delete path is rejected");
}

} // namespace

void RunPlatformTests()
{
    TestWin32HandleOwnership();
    TestFindHandleOwnership();
    TestCriticalSectionOwnership();
    TestGdiOwnership();
    TestRegistryAndComOwnership();
    TestImgSettingsTempDirectory();
    TestBrowsePathClassification();
    TestImgBufferValidationAndMapping();
    TestFileOperationPathList();
    TestFileOperationFlags();
    TestFileOperationResults();
}

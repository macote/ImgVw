#include "FileOperations.h"
#include "ImgFileList.h"
#include "ImgJpegDecoder.h"
#include "ImgRenderer.h"
#include "ImgLoader.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
int failures = 0;
SHFILEOPSTRUCTW captured_file_operation{};
std::vector<wchar_t> captured_paths;
int shell_result = 0;
BOOL shell_aborted = FALSE;

std::vector<unsigned char> CreateJpeg(bool cmyk, bool include_metadata)
{
    jpeg_compress_struct compressor{};
    jpeg_error_mgr error_manager{};
    compressor.err = jpeg_std_error(&error_manager);
    jpeg_create_compress(&compressor);

    unsigned char* compressed_data{};
    unsigned long compressed_size{};
    jpeg_mem_dest(&compressor, &compressed_data, &compressed_size);

    compressor.image_width = 4;
    compressor.image_height = 2;
    compressor.input_components = cmyk ? 4 : 3;
    compressor.in_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 90, TRUE);
    jpeg_start_compress(&compressor, TRUE);

    if (include_metadata)
    {
        const unsigned char exif[] = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0};
        const unsigned char icc[] = {1, 2, 3, 4, 5, 6};
        jpeg_write_marker(&compressor, JPEG_APP0 + 1, exif, sizeof(exif));
        jpeg_write_icc_profile(&compressor, icc, sizeof(icc));
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(compressor.image_width) * compressor.input_components);
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<unsigned char>((index * 31) & 0xFF);
    }

    while (compressor.next_scanline < compressor.image_height)
    {
        auto row = pixels.data();
        jpeg_write_scanlines(&compressor, &row, 1);
    }

    jpeg_finish_compress(&compressor);
    std::vector<unsigned char> result(compressed_data, compressed_data + compressed_size);
    jpeg_destroy_compress(&compressor);
    std::free(compressed_data);
    return result;
}

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

void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

void TestEmptyList()
{
    ImgFileList files(1);

    Check(files.Empty(), "new list is empty");
    Check(files.CurrentPath().empty(), "empty list has no current path");
    Check(!files.MoveToNext(), "empty list cannot move next");
    Check(!files.MoveToPrevious(), "empty list cannot move previous");
    Check(!files.MoveToFirst(), "empty list cannot move first");
    Check(!files.MoveToLast(), "empty list cannot move last");
    Check(!files.MoveToRandom(), "empty list cannot move randomly");
    Check(!files.RemoveCurrent(), "empty list cannot remove current item");
}

void TestOrderedNavigation()
{
    ImgFileList files(1);

    Check(files.Add(L"b.jpg"), "first path is added");
    Check(files.Add(L"a.jpg"), "second path is added");
    Check(files.Add(L"c.jpg"), "third path is added");
    Check(!files.Add(L"b.jpg"), "duplicate path is rejected");
    Check(files.CurrentPath() == L"b.jpg", "first collected path remains current");

    Check(files.MoveToFirst(), "move to first succeeds");
    Check(files.CurrentPath() == L"a.jpg", "first path uses sorted order");
    Check(!files.MoveToPrevious(), "cannot move before first");
    Check(files.MoveToNext(), "move next succeeds");
    Check(files.CurrentPath() == L"b.jpg", "next path is selected");
    Check(files.MoveToLast(), "move to last succeeds");
    Check(files.CurrentPath() == L"c.jpg", "last path is selected");
    Check(!files.MoveToNext(), "cannot move after last");
    Check(files.MoveTo(L"a.jpg"), "move to known path succeeds");
    Check(!files.MoveTo(L"missing.jpg"), "move to unknown path fails");
    Check(files.CurrentPath() == L"a.jpg", "failed move preserves current path");
}

void TestRemoval()
{
    ImgFileList files(1);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");

    Check(files.MoveTo(L"b.jpg"), "select middle item before removal");
    Check(files.RemoveCurrent(), "remove middle item succeeds");
    Check(files.CurrentPath() == L"c.jpg", "removal selects following item");
    Check(files.RemoveCurrent(), "remove last item succeeds");
    Check(files.CurrentPath().empty(), "removing last item parks at end");
    Check(files.MoveToPrevious(), "can move to previous item after removing last");
    Check(files.CurrentPath() == L"a.jpg", "previous item remains available");
    Check(files.RemoveCurrent(), "remove final item succeeds");
    Check(files.Empty(), "list is empty after removing all items");
}

void TestRandomNavigation()
{
    ImgFileList files(7);
    const std::set<std::wstring> expected = {L"a.jpg", L"b.jpg", L"c.jpg"};
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.Add(L"c.jpg");

    std::set<std::wstring> firstcycle;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        Check(files.MoveToRandom(), "random move succeeds");
        firstcycle.insert(files.CurrentPath());
    }

    Check(firstcycle == expected, "random cycle visits every item once");
    const auto last = files.CurrentPath();
    Check(files.MoveToRandom(), "next random cycle starts");
    Check(files.CurrentPath() != last, "random cycles do not repeat the boundary item");
}

void TestClear()
{
    ImgFileList files(1);
    files.Add(L"a.jpg");
    files.Add(L"b.jpg");
    files.MoveToRandom();

    files.Clear();

    Check(files.Empty(), "clear removes all items");
    Check(files.CurrentPath().empty(), "clear resets current path");
    Check(!files.MoveToRandom(), "clear resets random navigation");
}

void TestLoaderShutdown()
{
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        ImgLoader loader;
        loader.StopLoading();
        loader.StopLoading();
    }
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

void TestRendererInputValidation()
{
    ImgRenderer renderer;
    const auto result = renderer.Render({});

    Check(result.status == ImgRenderStatus::InvalidInput, "renderer rejects missing GDI resources");
    Check(result.win32_error == ERROR_INVALID_PARAMETER, "renderer reports invalid input error");
}

void TestRendererDrawsImageAndBackground()
{
    const auto targetdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bitmapinfo{};
    bitmapinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapinfo.bmiHeader.biWidth = 3;
    bitmapinfo.bmiHeader.biHeight = -3;
    bitmapinfo.bmiHeader.biPlanes = 1;
    bitmapinfo.bmiHeader.biBitCount = 32;
    bitmapinfo.bmiHeader.biCompression = BI_RGB;

    void* targetbits = nullptr;
    const auto targetbitmap = CreateDIBSection(targetdc, &bitmapinfo, DIB_RGB_COLORS, &targetbits, nullptr, 0);
    const auto oldtarget = targetbitmap == nullptr ? nullptr : SelectObject(targetdc, targetbitmap);

    bitmapinfo.bmiHeader.biWidth = 1;
    bitmapinfo.bmiHeader.biHeight = -1;
    void* sourcebits = nullptr;
    const auto sourcebitmap = CreateDIBSection(targetdc, &bitmapinfo, DIB_RGB_COLORS, &sourcebits, nullptr, 0);
    const auto background = CreateSolidBrush(RGB(0, 0, 0));

    Check(targetdc != nullptr && targetbitmap != nullptr && sourcebitmap != nullptr && background != nullptr,
          "renderer test GDI resources are created");
    if (targetdc != nullptr && targetbitmap != nullptr && sourcebitmap != nullptr && background != nullptr)
    {
        *static_cast<DWORD*>(sourcebits) = 0x00FFFFFF;
        const ImgRenderInput input{targetdc, background, {0, 0, 3, 3}, sourcebitmap, 1, 1, 1, 1};
        const auto result = ImgRenderer().Render(input);

        Check(result.Succeeded(), "renderer draws valid input");
        Check(GetPixel(targetdc, 1, 1) == RGB(255, 255, 255), "renderer copies the image bitmap");
        Check(GetPixel(targetdc, 0, 0) == RGB(0, 0, 0), "renderer fills outside the image");
    }

    if (oldtarget != nullptr && oldtarget != HGDI_ERROR)
    {
        SelectObject(targetdc, oldtarget);
    }
    if (background != nullptr)
    {
        DeleteObject(background);
    }
    if (sourcebitmap != nullptr)
    {
        DeleteObject(sourcebitmap);
    }
    if (targetbitmap != nullptr)
    {
        DeleteObject(targetbitmap);
    }
    if (targetdc != nullptr)
    {
        DeleteDC(targetdc);
    }
}

void TestJpegDecoderMetadataAndScaling()
{
    const auto jpeg = CreateJpeg(false, true);
    ImgJpegDecoder decoder;

    Check(decoder.Initialize(jpeg.data(), jpeg.size()), "JPEG decoder reads generated image");
    Check(decoder.width() == 4 && decoder.height() == 2, "JPEG decoder reports source dimensions");
    Check(!decoder.is_cmyk(), "RGB JPEG is not reported as CMYK");
    Check(decoder.exif_data() != nullptr && decoder.exif_size() == 10, "JPEG decoder exposes EXIF marker");
    Check(decoder.icc_profile() == std::vector<unsigned char>({1, 2, 3, 4, 5, 6}),
          "JPEG decoder assembles ICC profile");
    Check(decoder.ConfigureOutput(1, 2, false), "JPEG decoder accepts scaled BGR output");
    Check(decoder.output_width() == 2 && decoder.output_height() == 1, "JPEG decoder calculates scaled dimensions");

    const auto stride = ((decoder.output_width() * 3) + 3) & ~3;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * decoder.output_height());
    Check(decoder.Decode(pixels.data(), stride, true), "JPEG decoder writes scaled bottom-up pixels");
}

void TestJpegDecoderCmyk()
{
    const auto jpeg = CreateJpeg(true, false);
    ImgJpegDecoder decoder;

    Check(decoder.Initialize(jpeg.data(), jpeg.size()), "JPEG decoder reads generated CMYK image");
    Check(decoder.is_cmyk(), "CMYK JPEG is detected");
    Check(decoder.ConfigureOutput(1, 1, true), "JPEG decoder accepts CMYK output");

    const auto stride = ((decoder.output_width() * 4) + 3) & ~3;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * decoder.output_height());
    Check(decoder.Decode(pixels.data(), stride, true), "JPEG decoder writes CMYK pixels");
}

void TestJpegDecoderRejectsInvalidData()
{
    const unsigned char invalid[] = {1, 2, 3, 4};
    ImgJpegDecoder decoder;

    Check(!decoder.Initialize(invalid, sizeof(invalid)), "JPEG decoder rejects invalid input");
    Check(!decoder.error().empty(), "JPEG decoder reports invalid-input error");
}
} // namespace

int main()
{
    TestEmptyList();
    TestOrderedNavigation();
    TestRemoval();
    TestRandomNavigation();
    TestClear();
    TestLoaderShutdown();
    TestFileOperationPathList();
    TestFileOperationFlags();
    TestFileOperationResults();
    TestRendererInputValidation();
    TestRendererDrawsImageAndBackground();
    TestJpegDecoderMetadataAndScaling();
    TestJpegDecoderCmyk();
    TestJpegDecoderRejectsInvalidData();

    if (failures != 0)
    {
        std::cerr << failures << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}

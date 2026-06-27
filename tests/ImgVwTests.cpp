#include "ColorProfile.h"
#include "ColorTransform.h"
#include "FileOperations.h"
#include "ImageFormatDetector.h"
#include "ImgResampler.h"
#include "ImgFileList.h"
#include "ImgCache.h"
#include "ImgGDIItem.h"
#include "ImgItemFactory.h"
#include "ImgJpegDecoder.h"
#include "ImgRenderer.h"
#include "ImgLoader.h"

#include <lcms2.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
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

void WriteBytes(const std::wstring& path, const std::vector<unsigned char>& bytes)
{
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "test file can be created");
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD bytes_written{};
    Check(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &bytes_written, nullptr) &&
              bytes_written == bytes.size(),
          "test file bytes are written");
    CloseHandle(file);
}

std::wstring TempPath(const wchar_t* filename)
{
    wchar_t path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, path);
    return std::wstring(path) + filename;
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

void TestPathsFromCurrent()
{
    ImgFileList files(1);
    files.Add(L"b.jpg");
    files.Add(L"a.jpg");
    files.Add(L"d.jpg");
    files.Add(L"c.jpg");

    Check(files.MoveTo(L"c.jpg"), "move to queue seed start succeeds");
    const auto paths = files.PathsFromCurrent();
    const std::vector<std::wstring> expected = {L"c.jpg", L"d.jpg", L"a.jpg", L"b.jpg"};
    Check(paths == expected, "paths from current start at current item and wrap in browse order");
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

void TestImgCacheKeyUsesViewport()
{
    const ImgCacheKey first{L"C:\\images\\photo.jpg", 800, 600};
    const ImgCacheKey same{L"C:\\images\\photo.jpg", 800, 600};
    const ImgCacheKey wider{L"C:\\images\\photo.jpg", 1024, 600};
    const ImgCacheKey taller{L"C:\\images\\photo.jpg", 800, 768};
    const ImgCacheKey other_path{L"C:\\images\\other.jpg", 800, 600};

    Check(!(first < same) && !(same < first), "matching cache keys compare equivalent");
    Check(first < wider || wider < first, "cache key includes target width");
    Check(first < taller || taller < first, "cache key includes target height");
    Check(first < other_path || other_path < first, "cache key includes filepath");
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

void TestImageFormatDetectorSignatures()
{
    const unsigned char jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    Check(ImageFormatDetector::Detect(jpeg, sizeof(jpeg)) == DetectedImageFormat::JPEG, "JPEG signature is detected");

    const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Check(ImageFormatDetector::Detect(png, sizeof(png)) == DetectedImageFormat::PNG, "PNG signature is detected");

    const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    Check(ImageFormatDetector::Detect(gif, sizeof(gif)) == DetectedImageFormat::GIF, "GIF signature is detected");

    const unsigned char bmp[] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    Check(ImageFormatDetector::Detect(bmp, sizeof(bmp)) == DetectedImageFormat::BMP, "BMP signature is detected");

    const unsigned char tiff[] = {'I', 'I', 0x2A, 0x00};
    Check(ImageFormatDetector::Detect(tiff, sizeof(tiff)) == DetectedImageFormat::TIFF, "TIFF signature is detected");

    const unsigned char ico[] = {0, 0, 1, 0, 1, 0};
    Check(ImageFormatDetector::Detect(ico, sizeof(ico)) == DetectedImageFormat::ICO, "ICO header is detected");

    const unsigned char heif[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c', 0, 0, 0, 0, 'm', 'i', 'f', '1'};
    Check(ImageFormatDetector::Detect(heif, sizeof(heif)) == DetectedImageFormat::HEIF, "HEIF brand is detected");

    const unsigned char mp4[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 0, 0, 'm', 'p', '4', '2'};
    Check(ImageFormatDetector::Detect(mp4, sizeof(mp4)) == DetectedImageFormat::Unknown,
          "generic MP4 is not detected as HEIF");

    const unsigned char short_png[] = {0x89, 'P', 'N'};
    Check(ImageFormatDetector::Detect(short_png, sizeof(short_png)) == DetectedImageFormat::Unknown,
          "truncated signatures are unknown");
}

void TestImgItemFactoryResolvesSupportedExtensionsOnly()
{
    const auto png_named_heic = TempPath(L"imgvw_png_named_heic.heic");
    const auto jpeg_named_png = TempPath(L"imgvw_jpeg_named_png.png");
    const auto random_named_jpg = TempPath(L"imgvw_random_named_jpg.jpg");
    const auto jpeg_named_bin = TempPath(L"imgvw_jpeg_named_bin.bin");

    WriteBytes(png_named_heic, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    WriteBytes(jpeg_named_png, {0xFF, 0xD8, 0xFF, 0xE0});
    WriteBytes(random_named_jpg, {1, 2, 3, 4});
    WriteBytes(jpeg_named_bin, {0xFF, 0xD8, 0xFF, 0xE0});

    Check(ImgItemFactory::ResolveFormat(png_named_heic) == ImgItem::Format::PNG,
          "PNG bytes with supported HEIC extension route to GDI PNG path");
    Check(ImgItemFactory::ResolveFormat(jpeg_named_png) == ImgItem::Format::JPEG,
          "JPEG bytes with supported PNG extension route to JPEG path");
    Check(ImgItemFactory::ResolveFormat(random_named_jpg) == ImgItem::Format::JPEG,
          "unknown bytes with supported extension use extension fallback");
    Check(ImgItemFactory::ResolveFormat(jpeg_named_bin) == ImgItem::Format::Unsupported,
          "supported bytes with unsupported extension are not probed");

    DeleteFileW(png_named_heic.c_str());
    DeleteFileW(jpeg_named_png.c_str());
    DeleteFileW(random_named_jpg.c_str());
    DeleteFileW(jpeg_named_bin.c_str());
}

void TestGdiItemPreservesTopRowOrientation()
{
    const auto bmp_path = TempPath(L"imgvw_orientation.bmp");
    const std::vector<unsigned char> bmp = {
        'B', 'M', 62, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 2,   0, 0, 0, 1, 0, 24,  0, 0, 0,
        0,   0,   8,  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 0, 255, 0, 0, 0,
    };
    WriteBytes(bmp_path, bmp);

    ULONG_PTR gdiplus_token{};
    Gdiplus::GdiplusStartupInput gdiplus_startup_input;
    Check(Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_startup_input, nullptr) == Gdiplus::Ok,
          "GDI+ starts for GDI item orientation test");

    ImgGDIItem item(bmp_path, 1, 2);
    item.Load();
    Check(item.status() == ImgItem::Status::Ready, "GDI item loads orientation test BMP");

    if (item.status() == ImgItem::Status::Ready)
    {
        const auto bitmap = item.GetDisplayBitmap();
        const auto dc = CreateCompatibleDC(nullptr);
        Check(dc != nullptr, "orientation test memory DC is created");
        if (dc != nullptr)
        {
            const auto oldbitmap = SelectObject(dc, bitmap.bitmap());
            Check(GetPixel(dc, 0, 0) == RGB(255, 0, 0), "GDI item top row remains the image top row");
            if (oldbitmap != nullptr && oldbitmap != HGDI_ERROR)
            {
                SelectObject(dc, oldbitmap);
            }
            DeleteDC(dc);
        }
    }

    if (gdiplus_token != 0)
    {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }
    DeleteFileW(bmp_path.c_str());
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

void TestImgResamplerAreaDownscale()
{
    const std::vector<unsigned char> source = {
        0,   0,   0,   255, 100, 0,   0,   255, 0,   0,   0,   255, 100, 0,   0,   255,
        0,   100, 0,   255, 100, 100, 0,   255, 0,   100, 0,   255, 100, 100, 0,   255,
        0,   0,   0,   255, 100, 0,   0,   255, 0,   0,   0,   255, 100, 0,   0,   255,
        0,   100, 0,   255, 100, 100, 0,   255, 0,   100, 0,   255, 100, 100, 0,   255,
    };
    ImgResampler::Result result;

    Check(ImgResampler::DownscaleRgba8(source.data(), 16, 4, 4, 2, 2, ImgResampler::AlphaMode::Straight, &result),
          "area resampler downscales RGBA pixels");
    Check(result.stride == 8 && result.pixels.size() == 16, "area resampler returns packed RGBA dimensions");
    for (std::size_t pixel = 0; pixel < result.pixels.size(); pixel += 4)
    {
        Check(result.pixels[pixel] == 50 && result.pixels[pixel + 1] == 50 && result.pixels[pixel + 2] == 0 &&
                  result.pixels[pixel + 3] == 255,
              "area resampler averages the complete source footprint");
    }
}

void TestImgResamplerPremultipliesAlpha()
{
    const unsigned char source[] = {255, 0, 0, 255, 0, 0, 255, 0};
    ImgResampler::Result result;

    Check(ImgResampler::DownscaleRgba8(source, sizeof(source), 2, 1, 1, 1, ImgResampler::AlphaMode::Straight, &result),
          "area resampler handles straight alpha");
    Check(result.pixels == std::vector<unsigned char>({128, 0, 0, 128}),
          "area resampler filters premultiplied color and alpha");
}

void TestImgResamplerValidatesInput()
{
    const unsigned char source[] = {0, 0, 0, 255};
    ImgResampler::Result result;

    Check(!ImgResampler::DownscaleRgba8(source, sizeof(source), 1, 1, 2, 1, ImgResampler::AlphaMode::Straight,
                                        &result),
          "area resampler rejects upscaling");
    Check(!ImgResampler::DownscaleRgba8(source, 3, 1, 1, 1, 1, ImgResampler::AlphaMode::Straight, &result),
          "area resampler rejects a short source stride");
}

void TestBundledCmykProfile()
{
    std::ifstream stream("resources/color/CGATS21_CRPC5.icc", std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        stream.open("../resources/color/CGATS21_CRPC5.icc", std::ios::binary | std::ios::ate);
    }

    Check(stream.is_open(), "bundled CMYK profile is present");
    if (!stream.is_open())
    {
        return;
    }

    const auto size = stream.tellg();
    Check(size == 3339888, "bundled CMYK profile has the validated size");
    if (size <= 0)
    {
        return;
    }

    stream.seekg(0);
    std::vector<unsigned char> profile_data(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(profile_data.data()), static_cast<std::streamsize>(size));
    Check(stream.good(), "bundled CMYK profile can be read");

    const auto profile = cmsOpenProfileFromMem(profile_data.data(), static_cast<cmsUInt32Number>(profile_data.size()));
    Check(profile != nullptr, "Little CMS opens the bundled CMYK profile");
    if (profile == nullptr)
    {
        return;
    }

    Check(cmsGetColorSpace(profile) == cmsSigCmykData, "bundled profile uses the CMYK color space");
    const unsigned char expected_profile_id[] = {0xB0, 0xEB, 0x43, 0x15, 0x9B, 0xF2, 0x29, 0xC6,
                                                 0xEE, 0xDF, 0xAA, 0xAA, 0xA5, 0x4F, 0xF4, 0x97};
    unsigned char profile_id[sizeof(expected_profile_id)]{};
    cmsGetHeaderProfileID(profile, profile_id);
    Check(std::memcmp(profile_id, expected_profile_id, sizeof(profile_id)) == 0,
          "bundled profile has the validated ICC profile ID");

    auto color_profile =
        ColorProfile::OpenFromMemory(profile_data.data(), static_cast<cmsUInt32Number>(profile_data.size()));
    Check(color_profile.IsValid(), "ColorProfile opens the bundled CMYK profile");
    Check(color_profile.IsCmyk(), "ColorProfile validates the bundled CMYK color space");

    auto heap = GetProcessHeap();
    constexpr INT source_stride = 4;
    constexpr INT destination_stride = 4;
    auto transform_source = reinterpret_cast<PBYTE>(HeapAlloc(heap, 0, source_stride));
    Check(transform_source != nullptr, "CMYK transform test buffer is allocated");
    if (transform_source != nullptr)
    {
        transform_source[0] = 0;
        transform_source[1] = 0;
        transform_source[2] = 0;
        transform_source[3] = 0;
        const auto transform_result = ColorTransform::TransformCmyk8ReversedToBgr8(
            color_profile, 1, 1, source_stride, destination_stride, &transform_source, heap);
        Check(transform_result.Succeeded(), "ColorTransform converts reversed CMYK to BGR");
        HeapFree(heap, 0, transform_source);
    }

    const auto srgb_profile = cmsCreate_sRGBProfile();
    const auto transform =
        cmsCreateTransform(profile, TYPE_CMYK_8_REV, srgb_profile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
    Check(transform != nullptr, "bundled profile creates the required CMYK-to-BGR transform");

    if (transform != nullptr)
    {
        cmsDeleteTransform(transform);
    }
    if (srgb_profile != nullptr)
    {
        cmsCloseProfile(srgb_profile);
    }
    cmsCloseProfile(profile);
}
} // namespace

int main()
{
    TestEmptyList();
    TestOrderedNavigation();
    TestPathsFromCurrent();
    TestRemoval();
    TestRandomNavigation();
    TestClear();
    TestImgCacheKeyUsesViewport();
    TestLoaderShutdown();
    TestImageFormatDetectorSignatures();
    TestImgItemFactoryResolvesSupportedExtensionsOnly();
    TestGdiItemPreservesTopRowOrientation();
    TestFileOperationPathList();
    TestFileOperationFlags();
    TestFileOperationResults();
    TestRendererInputValidation();
    TestRendererDrawsImageAndBackground();
    TestJpegDecoderMetadataAndScaling();
    TestJpegDecoderCmyk();
    TestJpegDecoderRejectsInvalidData();
    TestImgResamplerAreaDownscale();
    TestImgResamplerPremultipliesAlpha();
    TestImgResamplerValidatesInput();
    TestBundledCmykProfile();

    if (failures != 0)
    {
        std::cerr << failures << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}

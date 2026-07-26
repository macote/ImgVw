#include "ColorProfile.h"
#include "ColorTransform.h"
#include "CompatibleDeviceContext.h"
#include "ImgCache.h"
#include "ImgGDIItem.h"
#include "ImgJPEGDecoder.h"
#include "SelectedGdiObject.h"
#include "../support/JpegFixture.h"
#include "../support/TempFile.h"
#include "../support/TestHarness.h"

#include <Windows.h>
#include <lcms2.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
HANDLE WINAPI FailLoadedEventCreate(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR)
{
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
}

BOOL WINAPI FailLoadedEventSignal(HANDLE)
{
    SetLastError(ERROR_WRITE_FAULT);
    return FALSE;
}

BOOL WINAPI FailLoadedEventReset(HANDLE)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

class CompletionTestImgItem final : public ImgItem
{
  public:
    CompletionTestImgItem(LoadedEventCreate createevent, LoadedEventSignal signalevent, LoadedEventReset resetevent)
        : ImgItem(L"completion-test.png", 800, 600, createevent, signalevent, resetevent)
    {
    }

    void Load() override
    {
        SetStatus(Status::Loading);
        SignalLoadComplete();
    }
};

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

void TestImgItemLoadCompletionResults()
{
    CompletionTestImgItem creation_failure(FailLoadedEventCreate, SetEvent, ResetEvent);
    const auto creation_result = creation_failure.loadcompletionresult();
    Check(creation_result.status == ImgItem::LoadCompletionStatus::EventCreationFailed &&
              creation_result.win32_error == ERROR_NOT_ENOUGH_MEMORY,
          "image item preserves loaded-event creation failures");
    creation_failure.Load();
    Check(creation_failure.loadcompletionresult().status == ImgItem::LoadCompletionStatus::EventCreationFailed,
          "signaling an unavailable loaded event preserves its creation failure");

    CompletionTestImgItem signal_failure(CreateEventW, FailLoadedEventSignal, ResetEvent);
    signal_failure.Load();
    const auto signal_result = signal_failure.loadcompletionresult();
    Check(signal_result.status == ImgItem::LoadCompletionStatus::SignalFailed &&
              signal_result.win32_error == ERROR_WRITE_FAULT,
          "image item preserves loaded-event signal failures");

    CompletionTestImgItem reset_failure(CreateEventW, SetEvent, FailLoadedEventReset);
    reset_failure.Load();
    Check(reset_failure.loadcompletionresult().Succeeded(), "image item reports a successful load signal");
    reset_failure.Unload();
    const auto reset_result = reset_failure.loadcompletionresult();
    Check(reset_result.status == ImgItem::LoadCompletionStatus::ResetFailed &&
              reset_result.win32_error == ERROR_INVALID_HANDLE,
          "image item preserves loaded-event reset failures");

    CompletionTestImgItem success(CreateEventW, SetEvent, ResetEvent);
    success.Load();
    Check(success.loadcompletionresult().Succeeded(), "image item exposes successful load completion");
    success.Unload();
    Check(success.loadcompletionresult().status == ImgItem::LoadCompletionStatus::Pending,
          "image item returns to pending completion after unload");
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
    Check(item.loadcompletionresult().Succeeded(), "GDI item publishes load completion through ImgItem");

    const auto display_state = item.GetDisplayState();
    Check(display_state.status == ImgItem::Status::Ready && display_state.frame != nullptr,
          "GDI item publishes a complete display frame");
    if (display_state.frame != nullptr)
    {
        Check(display_state.frame->width() == 1 && display_state.frame->height() == 2 &&
                  display_state.frame->offsetx() == 0 && display_state.frame->offsety() == 0,
              "published display frame contains coherent geometry");
        item.Unload();
        Check(item.status() == ImgItem::Status::Queued && item.GetDisplayState().frame == nullptr,
              "unloading removes the published frame");

        const auto bitmap = display_state.frame->GetBitmap();
        CompatibleDeviceContext dc(CreateCompatibleDC(nullptr));
        Check(dc.valid(), "orientation test memory DC is created");
        if (dc.valid())
        {
            SelectedGdiObject selected_bitmap(dc.get(), bitmap.bitmap());
            Check(selected_bitmap.valid(), "orientation test bitmap is selected");
            Check(GetPixel(dc.get(), 0, 0) == RGB(255, 0, 0), "GDI item top row remains the image top row");
        }
        Check(display_state.frame->buffersize() > 0, "captured display frame remains valid after unload");
    }

    if (gdiplus_token != 0)
    {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }
    DeleteFileW(bmp_path.c_str());
}

void TestJpegDecoderMetadataAndScaling()
{
    const auto jpeg = CreateJpeg(false, true);
    ImgJPEGDecoder decoder;

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
    ImgJPEGDecoder decoder;

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
    ImgJPEGDecoder decoder;

    Check(!decoder.Initialize(invalid, sizeof(invalid)), "JPEG decoder rejects invalid input");
    Check(!decoder.error().empty(), "JPEG decoder reports invalid-input error");
}

void TestBundledCmykProfile()
{
    const auto profile_path = GetFileAttributesW(L"resources/color/CGATS21_CRPC5.icc") != INVALID_FILE_ATTRIBUTES
                                  ? L"resources/color/CGATS21_CRPC5.icc"
                                  : L"../resources/color/CGATS21_CRPC5.icc";
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

    const auto validation = ImgItem::ValidateCMYKICCProfile(profile_path);
    Check(validation.status == ImgItem::CmykProfileValidationStatus::Valid && validation.Succeeded(),
          "CMYK profile validation reports a valid profile explicitly");

    const auto missing_profile_path = TempPath(L"imgvw-missing-profile.icc");
    DeleteFileW(missing_profile_path.c_str());
    const auto missing_validation = ImgItem::ValidateCMYKICCProfile(missing_profile_path);
    Check(missing_validation.status == ImgItem::CmykProfileValidationStatus::FileAccessFailed &&
              missing_validation.win32_error == ERROR_FILE_NOT_FOUND,
          "CMYK profile validation preserves file-access failures");

    const auto invalid_profile_path = TempPath(L"imgvw-invalid-profile.icc");
    WriteBytes(invalid_profile_path, {0x01, 0x02, 0x03, 0x04});
    const auto invalid_validation = ImgItem::ValidateCMYKICCProfile(invalid_profile_path);
    Check(invalid_validation.status == ImgItem::CmykProfileValidationStatus::InvalidProfile &&
              invalid_validation.win32_error == ERROR_SUCCESS,
          "CMYK profile validation distinguishes invalid profile data");
    DeleteFileW(invalid_profile_path.c_str());

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
    Check(srgb_profile != nullptr, "Little CMS creates an sRGB profile for validation");
    if (srgb_profile != nullptr)
    {
        cmsUInt32Number srgb_size{};
        Check(cmsSaveProfileToMem(srgb_profile, nullptr, &srgb_size) != FALSE && srgb_size > 0,
              "Little CMS reports the serialized sRGB profile size");
        std::vector<unsigned char> srgb_data(srgb_size);
        Check(cmsSaveProfileToMem(srgb_profile, srgb_data.data(), &srgb_size) != FALSE,
              "Little CMS serializes an sRGB profile");
        const auto srgb_profile_path = TempPath(L"imgvw-srgb-profile.icc");
        WriteBytes(srgb_profile_path, srgb_data);
        const auto srgb_validation = ImgItem::ValidateCMYKICCProfile(srgb_profile_path);
        Check(srgb_validation.status == ImgItem::CmykProfileValidationStatus::WrongColorSpace,
              "CMYK profile validation distinguishes a non-CMYK profile");
        DeleteFileW(srgb_profile_path.c_str());
    }

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

void RunImageTests()
{
    TestImgCacheKeyUsesViewport();
    TestImgItemLoadCompletionResults();
    TestGdiItemPreservesTopRowOrientation();
    TestJpegDecoderMetadataAndScaling();
    TestJpegDecoderCmyk();
    TestJpegDecoderRejectsInvalidData();
    TestBundledCmykProfile();
}

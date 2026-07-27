#include "ImgFormatDetector.h"
#include "ImgFormatResolver.h"
#include "ImgItemHelper.h"
#include "ImgResampler.h"
#include "../support/TempFile.h"
#include "../support/TestHarness.h"

#include <Windows.h>

#include <limits>
#include <vector>

namespace
{
void TestImageFormatDetectorSignatures()
{
    const unsigned char jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    Check(ImgFormatDetector::Detect(jpeg, sizeof(jpeg)) == DetectedImageFormat::JPEG, "JPEG signature is detected");

    const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Check(ImgFormatDetector::Detect(png, sizeof(png)) == DetectedImageFormat::PNG, "PNG signature is detected");

    const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    Check(ImgFormatDetector::Detect(gif, sizeof(gif)) == DetectedImageFormat::GIF, "GIF signature is detected");

    const unsigned char bmp[] = {'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    Check(ImgFormatDetector::Detect(bmp, sizeof(bmp)) == DetectedImageFormat::BMP, "BMP signature is detected");

    const unsigned char tiff[] = {'I', 'I', 0x2A, 0x00};
    Check(ImgFormatDetector::Detect(tiff, sizeof(tiff)) == DetectedImageFormat::TIFF, "TIFF signature is detected");

    const unsigned char ico[] = {0, 0, 1, 0, 1, 0};
    Check(ImgFormatDetector::Detect(ico, sizeof(ico)) == DetectedImageFormat::ICO, "ICO header is detected");

    const unsigned char heif[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c', 0, 0, 0, 0, 'm', 'i', 'f', '1'};
    Check(ImgFormatDetector::Detect(heif, sizeof(heif)) == DetectedImageFormat::HEIF, "HEIF brand is detected");

    const unsigned char mp4[] = {0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 0, 0, 'm', 'p', '4', '2'};
    Check(ImgFormatDetector::Detect(mp4, sizeof(mp4)) == DetectedImageFormat::Unknown,
          "generic MP4 is not detected as HEIF");

    const unsigned char short_png[] = {0x89, 'P', 'N'};
    Check(ImgFormatDetector::Detect(short_png, sizeof(short_png)) == DetectedImageFormat::Unknown,
          "truncated signatures are unknown");
}

void TestImageFormatResolverUsesSupportedExtensionsOnly()
{
    const auto png_named_heic = TempPath(L"imgvw_png_named_heic.heic");
    const auto jpeg_named_png = TempPath(L"imgvw_jpeg_named_png.png");
    const auto random_named_jpg = TempPath(L"imgvw_random_named_jpg.jpg");
    const auto jpeg_named_bin = TempPath(L"imgvw_jpeg_named_bin.bin");

    WriteBytes(png_named_heic, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    WriteBytes(jpeg_named_png, {0xFF, 0xD8, 0xFF, 0xE0});
    WriteBytes(random_named_jpg, {1, 2, 3, 4});
    WriteBytes(jpeg_named_bin, {0xFF, 0xD8, 0xFF, 0xE0});

    Check(ImgFormatResolver::Resolve(png_named_heic) == ImgItem::Format::PNG,
          "PNG bytes with supported HEIC extension route to GDI PNG path");
    Check(ImgFormatResolver::Resolve(jpeg_named_png) == ImgItem::Format::JPEG,
          "JPEG bytes with supported PNG extension route to JPEG path");
    Check(ImgFormatResolver::Resolve(random_named_jpg) == ImgItem::Format::JPEG,
          "unknown bytes with supported extension use extension fallback");
    Check(ImgFormatResolver::Resolve(jpeg_named_bin) == ImgItem::Format::Unsupported,
          "supported bytes with unsupported extension are not probed");

    DeleteFileW(png_named_heic.c_str());
    DeleteFileW(jpeg_named_png.c_str());
    DeleteFileW(random_named_jpg.c_str());
    DeleteFileW(jpeg_named_bin.c_str());
}

void TestImgResamplerAreaDownscale()
{
    const std::vector<unsigned char> source = {
        0,   0,   0, 255, 100, 0,   0,   255, 0,   0,   0, 255, 100, 0,   0,   255, 0,   100, 0, 255, 100, 100,
        0,   255, 0, 100, 0,   255, 100, 100, 0,   255, 0, 0,   0,   255, 100, 0,   0,   255, 0, 0,   0,   255,
        100, 0,   0, 255, 0,   100, 0,   255, 100, 100, 0, 255, 0,   100, 0,   255, 100, 100, 0, 255,
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

    Check(!ImgResampler::DownscaleRgba8(source, sizeof(source), 1, 1, 2, 1, ImgResampler::AlphaMode::Straight, &result),
          "area resampler rejects upscaling");
    Check(!ImgResampler::DownscaleRgba8(source, 3, 1, 1, 1, 1, ImgResampler::AlphaMode::Straight, &result),
          "area resampler rejects a short source stride");
    Check(!ImgResampler::DownscaleRgba8(source, (std::numeric_limits<std::size_t>::max)(), 32769, 1, 1, 1,
                                        ImgResampler::AlphaMode::Straight, &result),
          "area resampler rejects source dimensions beyond its checked limit");
    Check(!ImgResampler::DownscaleRgba8(source, 32768U * 4U, 32768, 32768, 32768, 32768,
                                        ImgResampler::AlphaMode::Straight, &result),
          "area resampler rejects output that exceeds its working-set limit");
}

void TestDisplaySizeCalculation()
{
    INT width{};
    INT height{};
    Check(ImgItemHelper::CalculateDisplaySize(4000, 2000, 1000, 1000, &width, &height) && width == 1000 &&
              height == 500,
          "display size fits a landscape image");
    Check(ImgItemHelper::CalculateDisplaySize(2000, 4000, 1000, 1000, &width, &height) && width == 500 &&
              height == 1000,
          "display size fits a portrait image");
    Check(ImgItemHelper::CalculateDisplaySize(320, 200, 1000, 1000, &width, &height) && width == 320 && height == 200,
          "display size does not upscale");
    Check(!ImgItemHelper::CalculateDisplaySize(0, 200, 1000, 1000, &width, &height),
          "display size rejects invalid source dimensions");
}
} // namespace

void RunImagePolicyTests()
{
    TestImageFormatDetectorSignatures();
    TestImageFormatResolverUsesSupportedExtensionsOnly();
    TestImgResamplerAreaDownscale();
    TestImgResamplerPremultipliesAlpha();
    TestImgResamplerValidatesInput();
    TestDisplaySizeCalculation();
}

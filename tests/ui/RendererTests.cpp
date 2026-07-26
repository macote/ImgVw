#include "CompatibleDeviceContext.h"
#include "GdiObject.h"
#include "ImgRenderer.h"
#include "SelectedGdiObject.h"
#include "../support/TestHarness.h"

#include <Windows.h>

namespace
{
void TestRendererInputValidation()
{
    ImgRenderer renderer;
    const auto result = renderer.Render({});

    Check(result.status == ImgRenderStatus::InvalidInput, "renderer rejects missing GDI resources");
    Check(result.win32_error == ERROR_INVALID_PARAMETER, "renderer reports invalid input error");
}

void TestRendererDrawsImageAndBackground()
{
    CompatibleDeviceContext targetdc(CreateCompatibleDC(nullptr));
    BITMAPINFO bitmapinfo{};
    bitmapinfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapinfo.bmiHeader.biWidth = 3;
    bitmapinfo.bmiHeader.biHeight = -3;
    bitmapinfo.bmiHeader.biPlanes = 1;
    bitmapinfo.bmiHeader.biBitCount = 32;
    bitmapinfo.bmiHeader.biCompression = BI_RGB;

    void* targetbits = nullptr;
    GdiObject<HBITMAP> targetbitmap(
        CreateDIBSection(targetdc.get(), &bitmapinfo, DIB_RGB_COLORS, &targetbits, nullptr, 0));
    SelectedGdiObject selected_target(targetdc.get(), targetbitmap.get());

    bitmapinfo.bmiHeader.biWidth = 1;
    bitmapinfo.bmiHeader.biHeight = -1;
    void* sourcebits = nullptr;
    GdiObject<HBITMAP> sourcebitmap(
        CreateDIBSection(targetdc.get(), &bitmapinfo, DIB_RGB_COLORS, &sourcebits, nullptr, 0));
    GdiObject<HBRUSH> background(CreateSolidBrush(RGB(0, 0, 0)));

    Check(targetdc.valid() && targetbitmap.valid() && selected_target.valid() && sourcebitmap.valid() &&
              background.valid(),
          "renderer test GDI resources are created");
    if (targetdc.valid() && targetbitmap.valid() && selected_target.valid() && sourcebitmap.valid() &&
        background.valid())
    {
        *static_cast<DWORD*>(sourcebits) = 0x00FFFFFF;
        Check(IntersectClipRect(targetdc.get(), 0, 0, 2, 2) != RGN_ERROR,
              "renderer test establishes a caller clip");
        const ImgRenderInput input{
            targetdc.get(), background.get(), {0, 0, 3, 3}, sourcebitmap.get(), 1, 1, 1, 1};
        const auto result = ImgRenderer().Render(input);

        Check(result.Succeeded(), "renderer draws valid input");
        RECT clipbox{};
        const RECT expectedclipbox{0, 0, 2, 2};
        Check(GetClipBox(targetdc.get(), &clipbox) != ERROR && EqualRect(&clipbox, &expectedclipbox),
              "renderer preserves the caller clip");
        Check(GetPixel(targetdc.get(), 1, 1) == RGB(255, 255, 255), "renderer copies the image bitmap");
        Check(GetPixel(targetdc.get(), 0, 0) == RGB(0, 0, 0), "renderer fills outside the image");
    }

    ImgRenderInput invalid_bitmap_input{
        targetdc.get(), background.get(), {0, 0, 3, 3}, reinterpret_cast<HBITMAP>(static_cast<ULONG_PTR>(1)),
        1,              1,                1,            1};
    const auto invalid_bitmap_result = ImgRenderer().Render(invalid_bitmap_input);
    Check(invalid_bitmap_result.status == ImgRenderStatus::SelectBitmapFailed,
          "renderer reports bitmap selection failure");
    if (invalid_bitmap_result.win32_error != ERROR_SUCCESS)
    {
        Check(invalid_bitmap_result.win32_error == ERROR_INVALID_HANDLE,
              "renderer retains the native bitmap selection error");
    }
}

} // namespace

void RunUiTests()
{
    TestRendererInputValidation();
    TestRendererDrawsImageAndBackground();
}

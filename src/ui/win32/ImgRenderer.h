#pragma once

#include <Windows.h>

struct ImgRenderInput
{
    HDC target_dc{nullptr};
    HBRUSH background_brush{nullptr};
    RECT client_rectangle{};
    HBITMAP bitmap{nullptr};
    int x{};
    int y{};
    int width{};
    int height{};
    BOOL has_protected_rectangle{FALSE};
    RECT protected_rectangle{};
};

enum class ImgRenderStatus
{
    Succeeded,
    InvalidInput,
    CreateMemoryDcFailed,
    SelectBitmapFailed,
    ExcludeClipFailed,
    FillBackgroundFailed,
    ResetClipFailed,
    CopyBitmapFailed
};

struct ImgRenderResult
{
    ImgRenderStatus status{ImgRenderStatus::InvalidInput};
    DWORD win32_error{};

    bool Succeeded() const
    {
        return status == ImgRenderStatus::Succeeded;
    }
};

class ImgRenderer final
{
  public:
    ImgRenderResult Render(const ImgRenderInput& input) const;
};

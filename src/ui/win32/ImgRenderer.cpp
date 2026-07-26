#include "ImgRenderer.h"

#include "CompatibleDeviceContext.h"
#include "SelectedGdiObject.h"

namespace
{
ImgRenderResult Failure(ImgRenderStatus status)
{
    return {status, GetLastError()};
}

void ClearLastError()
{
    SetLastError(ERROR_SUCCESS);
}

bool ExcludeProtectedRectangle(HDC dc, const ImgRenderInput& input)
{
    if (!input.has_protected_rectangle || IsRectEmpty(&input.protected_rectangle))
    {
        return true;
    }

    return ExcludeClipRect(dc, input.protected_rectangle.left, input.protected_rectangle.top,
                           input.protected_rectangle.right, input.protected_rectangle.bottom) != RGN_ERROR;
}
} // namespace

ImgRenderResult ImgRenderer::Render(const ImgRenderInput& input) const
{
    if (input.target_dc == nullptr || input.background_brush == nullptr || input.bitmap == nullptr ||
        input.width <= 0 || input.height <= 0)
    {
        return {ImgRenderStatus::InvalidInput, ERROR_INVALID_PARAMETER};
    }

    ClearLastError();
    CompatibleDeviceContext memorydc(CreateCompatibleDC(input.target_dc));
    if (!memorydc.valid())
    {
        return Failure(ImgRenderStatus::CreateMemoryDcFailed);
    }

    ClearLastError();
    SelectedGdiObject selectedbitmap(memorydc.get(), input.bitmap);
    if (!selectedbitmap.valid())
    {
        return Failure(ImgRenderStatus::SelectBitmapFailed);
    }

    ClearLastError();
    const auto backgroundsavedstate = SaveDC(input.target_dc);
    if (backgroundsavedstate == 0)
    {
        return Failure(ImgRenderStatus::SaveDcFailed);
    }

    ImgRenderResult result{ImgRenderStatus::Succeeded, ERROR_SUCCESS};
    ClearLastError();
    if (ExcludeClipRect(input.target_dc, input.x, input.y, input.x + input.width, input.y + input.height) ==
            RGN_ERROR ||
        !ExcludeProtectedRectangle(input.target_dc, input))
    {
        result = Failure(ImgRenderStatus::ExcludeClipFailed);
    }
    else
    {
        ClearLastError();
        if (FillRect(input.target_dc, &input.client_rectangle, input.background_brush) == 0)
        {
            result = Failure(ImgRenderStatus::FillBackgroundFailed);
        }
    }

    ClearLastError();
    if (!RestoreDC(input.target_dc, backgroundsavedstate))
    {
        result = Failure(ImgRenderStatus::RestoreDcFailed);
    }
    else if (result.Succeeded())
    {
        ClearLastError();
        const auto bitmapsavedstate = SaveDC(input.target_dc);
        if (bitmapsavedstate == 0)
        {
            result = Failure(ImgRenderStatus::SaveDcFailed);
        }
        else
        {
            ClearLastError();
            if (!ExcludeProtectedRectangle(input.target_dc, input))
            {
                result = Failure(ImgRenderStatus::ExcludeClipFailed);
            }
            else if (!BitBlt(input.target_dc, input.x, input.y, input.width, input.height, memorydc.get(), 0, 0,
                             SRCCOPY))
            {
                result = Failure(ImgRenderStatus::CopyBitmapFailed);
            }

            ClearLastError();
            if (!RestoreDC(input.target_dc, bitmapsavedstate))
            {
                result = Failure(ImgRenderStatus::RestoreDcFailed);
            }
        }
    }

    return result;
}

#include "ImgRenderer.h"

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
    const auto memorydc = CreateCompatibleDC(input.target_dc);
    if (memorydc == nullptr)
    {
        return Failure(ImgRenderStatus::CreateMemoryDcFailed);
    }

    ClearLastError();
    const auto replacedobject = SelectObject(memorydc, input.bitmap);
    if (replacedobject == nullptr || replacedobject == HGDI_ERROR)
    {
        const auto result = Failure(ImgRenderStatus::SelectBitmapFailed);
        DeleteDC(memorydc);
        return result;
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

        ClearLastError();
        if (SelectClipRgn(input.target_dc, nullptr) == RGN_ERROR)
        {
            result = Failure(ImgRenderStatus::ResetClipFailed);
        }
        else if (result.Succeeded())
        {
            ClearLastError();
            if (!ExcludeProtectedRectangle(input.target_dc, input))
            {
                result = Failure(ImgRenderStatus::ExcludeClipFailed);
            }
            else if (!BitBlt(input.target_dc, input.x, input.y, input.width, input.height, memorydc, 0, 0, SRCCOPY))
            {
                result = Failure(ImgRenderStatus::CopyBitmapFailed);
            }

            SelectClipRgn(input.target_dc, nullptr);
        }
    }

    SelectObject(memorydc, replacedobject);
    DeleteDC(memorydc);
    return result;
}

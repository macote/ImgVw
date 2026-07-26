#pragma once

#include "ImgRenderer.h"
#include "ImgItem.h"

#include <memory>
#include <string>

struct DisplaySnapshot
{
    std::shared_ptr<ImgItem> item;
    std::wstring path;
    ImgItem::DisplayState state;
};

enum class DisplayPresentation
{
    NoItem,
    WaitingForImage,
    ImageReady,
    ImageError,
    ImageNotReady,
    RenderFailed,
};

struct DisplayPresentationInput
{
    HDC dc{nullptr};
    HBRUSH background_brush{nullptr};
    RECT client_rectangle{};
    BOOL waiting_for_image{FALSE};
    BOOL first_paint{TRUE};
    BOOL has_protected_rectangle{FALSE};
    RECT protected_rectangle{};
};

struct DisplayPresentationResult
{
    DisplayPresentation presentation{DisplayPresentation::NoItem};
    ImgRenderResult render_result{};

    bool IsImageReady() const
    {
        return presentation == DisplayPresentation::ImageReady;
    }

    bool ShouldCompleteFirstPaint() const
    {
        return presentation != DisplayPresentation::WaitingForImage && presentation != DisplayPresentation::NoItem;
    }
};

class DisplayPresenter final
{
  public:
    static DisplayPresentation DecidePresentation(ImgItem::Status status, bool has_frame, bool waiting_for_image,
                                                  bool first_paint)
    {
        if (status == ImgItem::Status::Error)
        {
            return DisplayPresentation::ImageError;
        }
        if (status == ImgItem::Status::Ready && has_frame)
        {
            return DisplayPresentation::ImageReady;
        }
        return waiting_for_image || first_paint ? DisplayPresentation::WaitingForImage
                                                : DisplayPresentation::ImageNotReady;
    }

    static bool ShouldDrawOnlyOverlay(bool overlay_visible, ImgItem::Status status, const RECT& overlay_rectangle,
                                      const RECT& paint_rectangle)
    {
        return overlay_visible && status == ImgItem::Status::Ready && !IsRectEmpty(&overlay_rectangle) &&
               paint_rectangle.left >= overlay_rectangle.left && paint_rectangle.top >= overlay_rectangle.top &&
               paint_rectangle.right <= overlay_rectangle.right && paint_rectangle.bottom <= overlay_rectangle.bottom;
    }

    DisplayPresentationResult Present(const DisplaySnapshot& snapshot, const DisplayPresentationInput& input) const;

  private:
    ImgRenderer renderer_;
};

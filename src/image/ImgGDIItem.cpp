#include "ImgGDIItem.h"
#include "ImgItemHelper.h"

#include <limits>

void ImgGDIItem::Load()
{
    SetStatus(Status::Loading);

    const auto bitmap = std::make_unique<Gdiplus::Bitmap>(filepath_.c_str(), FALSE);
    lastgdiplusstatus_ = bitmap->GetLastStatus();
    if (lastgdiplusstatus_ != Gdiplus::Status::Ok || bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0 ||
        bitmap->GetWidth() > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
        bitmap->GetHeight() > static_cast<UINT>((std::numeric_limits<INT>::max)()))
    {
        SetError();
        goto done;
    }

    width_ = static_cast<INT>(bitmap->GetWidth());
    height_ = static_cast<INT>(bitmap->GetHeight());
    if (width_ > targetwidth_ || height_ > targetheight_)
    {
        pending_displaybuffer_ = ImgItemHelper::ResizeImage(bitmap.get(), targetwidth_, targetheight_);
    }
    else
    {
        pending_displaybuffer_ = ImgItemHelper::GetBuffer(bitmap.get());
    }

    SetupDisplayParameters(false);

done:

    SignalLoadComplete();
}

void ImgGDIItem::Unload()
{
    lastgdiplusstatus_ = Gdiplus::Status::Ok;
    ImgItem::Unload();
}

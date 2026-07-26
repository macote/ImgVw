#include "ImgGDIItem.h"
#include "ImgItemHelper.h"

void ImgGDIItem::Load()
{
    SetStatus(Status::Loading);

    const auto bitmap = std::make_unique<Gdiplus::Bitmap>(filepath_.c_str(), FALSE);
    lastgdiplusstatus_ = bitmap->GetLastStatus();
    if (lastgdiplusstatus_ != Gdiplus::Status::Ok || bitmap->GetWidth() == 0)
    {
        SetError();
        goto done;
    }

    width_ = bitmap->GetWidth();
    height_ = bitmap->GetHeight();
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

    SetEvent(loadedevent_.get());
}

void ImgGDIItem::Unload()
{
    lastgdiplusstatus_ = Gdiplus::Status::Ok;
    ImgItem::Unload();
}

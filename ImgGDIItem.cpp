#include "ImgGDIItem.h"
#include "ImgItemHelper.h"

void ImgGDIItem::Load()
{
    status_ = Status::Loading;

    const auto bitmap = std::make_unique<Gdiplus::Bitmap>(filepath_.c_str(), FALSE);
    lastgdiplusstatus_ = bitmap->GetLastStatus();
    if (lastgdiplusstatus_ != Gdiplus::Status::Ok || bitmap->GetWidth() == 0)
    {
        status_ = Status::Error;
        goto done;
    }

    width_ = bitmap->GetWidth();
    height_ = bitmap->GetHeight();
    if (width_ > targetwidth_ || height_ > targetheight_)
    {
        displaybuffer_ = ImgItemHelper::ResizeImage(bitmap.get(), targetwidth_, targetheight_);
    }
    else
    {
        displaybuffer_ = ImgItemHelper::GetBuffer(bitmap.get());
    }

    SetupDisplayParameters(true);

    status_ = Status::Ready;

done:

    SetEvent(loadedevent_);
}

void ImgGDIItem::Unload()
{
    lastgdiplusstatus_ = Gdiplus::Status::Ok;
    ImgItem::Unload();
}
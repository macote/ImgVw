#include "ImgItem.h"

void ImgItem::Unload()
{
    status_ = Status::Queued;
    ResetEvent(loadedevent_);
}

void ImgItem::SetupDisplayParameters(BOOL topdownbitmap)
{
    pbitmapinfo_ = reinterpret_cast<PBITMAPINFO>(HeapAlloc(heap_, 0, sizeof(BITMAPINFOHEADER)));
    pbitmapinfo_->bmiHeader.biCompression = BI_RGB;
    pbitmapinfo_->bmiHeader.biBitCount = 24;
    pbitmapinfo_->bmiHeader.biWidth = displaybuffer_.width();
    pbitmapinfo_->bmiHeader.biHeight = topdownbitmap ? -displaybuffer_.height() : displaybuffer_.height();
    pbitmapinfo_->bmiHeader.biPlanes = 1;
    pbitmapinfo_->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

    offsetx_ = (targetwidth_ - displaybuffer_.width()) / 2;
    offsety_ = (targetheight_ - displaybuffer_.height()) / 2;
}

ImgBitmap ImgItem::GetDisplayBitmap() const 
{
    auto filemap = displaybuffer_.GetFileMapView();
   
    return ImgBitmap(pbitmapinfo_, filemap.data(), displaybuffer_.buffersize());
}

void ImgItem::OpenICCProfile(PBYTE iccprofiledata, UINT iccprofiledatabytecount)
{
    iccprofile_ = cmsOpenProfileFromMem(iccprofiledata, iccprofiledatabytecount);
}

void ImgItem::TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer) const
{
    // TODO: determine if other types of transform (TYPE_CMYK_8_REV => TYPE_BGR_8) should be supported.
    auto srgbprofile = cmsCreate_sRGBProfile();
    auto transform = cmsCreateTransform(iccprofile_, TYPE_CMYK_8_REV, srgbprofile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
    auto newbuffer = reinterpret_cast<PBYTE>(HeapAlloc(heap_, 0, newstride * height));
    cmsDoTransformLineStride(transform, *buffer, newbuffer, width, height, stride, newstride, 0, 0);

    HeapFree(heap_, 0, *buffer);
    *buffer = newbuffer;

    cmsDeleteTransform(transform);
    cmsCloseProfile(srgbprofile);
}

void ImgItem::CloseICCProfile()
{
    if (iccprofile_ != nullptr)
    {
        cmsCloseProfile(iccprofile_);
        iccprofile_ = nullptr;
    }
}

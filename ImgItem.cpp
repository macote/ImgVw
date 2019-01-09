#include "ImgItem.h"
#include "resource.h"

cmsHPROFILE ImgItem::DefaultICCProfile = nullptr;

CRITICAL_SECTION ImgItem::DefaultICCProfileCriticalSection = CRITICAL_SECTION();

ImgItem::DefaultICCProfileCriticalSectionInitializer ImgItem::defaultICCProfileCriticalSectionInitializer;

void ImgItem::Unload()
{
    status_ = Status::Queued;
    CloseICCProfile();
    ResetEvent(loadedevent_);
}

void ImgItem::SetupDisplayParameters(BOOL topdownbitmap)
{
    pbitmapinfo_ = reinterpret_cast<PBITMAPINFO>(HeapAlloc(heap_, 0, sizeof(BITMAPINFOHEADER)));
    if (pbitmapinfo_ == NULL)
    {
        // TODO: handle error.
        return;
    }

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

void ImgItem::OpenICCProfile(const PBYTE iccprofiledata, UINT iccprofiledatabytecount)
{
	iccprofile_ = cmsOpenProfileFromMem(iccprofiledata, iccprofiledatabytecount);
}

void ImgItem::LoadDefaultICCProfile()
{
    EnterCriticalSection(&DefaultICCProfileCriticalSection);
    if (DefaultICCProfile == nullptr)
    {
        TCHAR appdatapath[260];
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdatapath)))
        {
            TCHAR imgvwappdatapath[260];
            PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath);

            TCHAR iccpath[260];
            PathCombine(iccpath, imgvwappdatapath, kDefaultICCProfileFilename);

            if (PathFileExists(iccpath))
            {
                FileMapView iccfilemap(std::wstring(iccpath), FileMapView::Mode::Read);
                DefaultICCProfile = cmsOpenProfileFromMem(iccfilemap.data(), iccfilemap.filesize().LowPart);
            }
        }
    }

    LeaveCriticalSection(&DefaultICCProfileCriticalSection);
}

void ImgItem::UnloadDefaultICCProfile()
{
    if (DefaultICCProfile != nullptr)
    {
        cmsCloseProfile(DefaultICCProfile);
        DefaultICCProfile = nullptr;
    }
}

BOOL ImgItem::TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer)
{
    if (iccprofile_ == nullptr && DefaultICCProfile == nullptr)
    {
        LoadDefaultICCProfile();
    }

    auto iccprofile = iccprofile_ != nullptr ? iccprofile_ : DefaultICCProfile;

    if (iccprofile == nullptr)
    {
        iccprofileloadfailed_ = TRUE;

        return FALSE;
    }

    // TODO: determine if other types of transform (TYPE_CMYK_8_REV => TYPE_BGR_8) should be supported.
    auto srgbprofile = cmsCreate_sRGBProfile();
    auto transform = cmsCreateTransform(iccprofile, TYPE_CMYK_8_REV, srgbprofile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
    if (transform == NULL)
    {
        iccprofileloadfailed_ = TRUE;
        cmsCloseProfile(srgbprofile);

        return FALSE;
    }

    auto newbuffer = reinterpret_cast<PBYTE>(HeapAlloc(heap_, 0, newstride * height));
    cmsDoTransformLineStride(transform, *buffer, newbuffer, width, height, stride, newstride, 0, 0);

    HeapFree(heap_, 0, *buffer);
    *buffer = newbuffer;

    cmsDeleteTransform(transform);
    cmsCloseProfile(srgbprofile);

    return TRUE;
}

void ImgItem::CloseICCProfile()
{
    if (iccprofile_ != nullptr)
    {
        cmsCloseProfile(iccprofile_);
        iccprofile_ = nullptr;
    }
}

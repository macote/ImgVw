#include "ImgItem.h"
#include "resource.h"

#include "ColorTransform.h"

ColorProfile ImgItem::DefaultICCProfile;

ImgItem::CmykProfileSource ImgItem::DefaultICCProfileSource = ImgItem::CmykProfileSource::None;

CRITICAL_SECTION ImgItem::DefaultICCProfileCriticalSection = CRITICAL_SECTION();

ImgItem::DefaultICCProfileCriticalSectionInitializer ImgItem::defaultICCProfileCriticalSectionInitializer;

void ImgItem::Unload()
{
    status_ = Status::Queued;
    iccprofileloadfailed_ = FALSE;
    cmykprofilesource_ = CmykProfileSource::None;
    CloseICCProfile();
    ResetEvent(loadedevent_);
}

void ImgItem::SetupDisplayParameters(BOOL topdownbitmap)
{
    if (pbitmapinfo_ != nullptr)
    {
        HeapFree(heap_, 0, pbitmapinfo_);
        pbitmapinfo_ = nullptr;
    }

    pbitmapinfo_ = reinterpret_cast<PBITMAPINFO>(HeapAlloc(heap_, 0, sizeof(BITMAPINFOHEADER)));
    if (pbitmapinfo_ == nullptr)
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

void ImgItem::OpenICCProfile(const BYTE* iccprofiledata, UINT iccprofiledatabytecount)
{
    iccprofile_ = ColorProfile::OpenFromMemory(iccprofiledata, iccprofiledatabytecount);
    if (iccprofile_.IsValid() && !iccprofile_.IsCmyk())
    {
        CloseICCProfile();
    }
    else if (iccprofile_.IsValid())
    {
        cmykprofilesource_ = CmykProfileSource::Embedded;
    }
}

void ImgItem::LoadDefaultICCProfile()
{
    EnterCriticalSection(&DefaultICCProfileCriticalSection);
    if (!DefaultICCProfile.IsValid())
    {
        try
        {
            TCHAR appdatapath[MAX_PATH]{};
            if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdatapath)))
            {
                TCHAR imgvwappdatapath[MAX_PATH]{};
                TCHAR iccpath[MAX_PATH]{};
                if (PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath) != nullptr &&
                    PathCombine(iccpath, imgvwappdatapath, kDefaultICCProfileFilename) != nullptr &&
                    PathFileExists(iccpath))
                {
                    FileMapView iccfilemap(std::wstring(iccpath), FileMapView::Mode::Read);
                    if (iccfilemap.filesize().HighPart == 0)
                    {
                        DefaultICCProfile =
                            ColorProfile::OpenFromMemory(iccfilemap.data(), iccfilemap.filesize().LowPart);
                    }
                }

                if (DefaultICCProfile.IsValid() && !DefaultICCProfile.IsCmyk())
                {
                    DefaultICCProfile.Reset();
                }
                else if (DefaultICCProfile.IsValid())
                {
                    DefaultICCProfileSource = CmykProfileSource::UserDefault;
                }
            }
        }
        catch (...)
        {
            DefaultICCProfile.Reset();
        }

        if (!DefaultICCProfile.IsValid())
        {
            const auto resource = FindResource(nullptr, MAKEINTRESOURCE(IDR_DEFAULT_CMYK_ICC), RT_RCDATA);
            const auto resource_size = resource == nullptr ? 0 : SizeofResource(nullptr, resource);
            const auto resource_handle = resource == nullptr ? nullptr : LoadResource(nullptr, resource);
            const auto resource_data = resource_handle == nullptr ? nullptr : LockResource(resource_handle);
            if (resource_data != nullptr && resource_size > 0)
            {
                DefaultICCProfile = ColorProfile::OpenFromMemory(resource_data, resource_size);
                if (DefaultICCProfile.IsValid() && !DefaultICCProfile.IsCmyk())
                {
                    DefaultICCProfile.Reset();
                }
                else if (DefaultICCProfile.IsValid())
                {
                    DefaultICCProfileSource = CmykProfileSource::BundledFallback;
                }
            }
        }
    }

    LeaveCriticalSection(&DefaultICCProfileCriticalSection);
}

void ImgItem::UnloadDefaultICCProfile()
{
    EnterCriticalSection(&DefaultICCProfileCriticalSection);
    DefaultICCProfile.Reset();
    DefaultICCProfileSource = CmykProfileSource::None;

    LeaveCriticalSection(&DefaultICCProfileCriticalSection);
}

BOOL ImgItem::ResetDefaultICCProfile()
{
    TCHAR appdatapath[MAX_PATH];
    if (FAILED(SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdatapath)))
    {
        return FALSE;
    }

    TCHAR imgvwappdatapath[MAX_PATH];
    if (PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath) == nullptr)
    {
        return FALSE;
    }

    TCHAR iccpath[MAX_PATH];
    if (PathCombine(iccpath, imgvwappdatapath, kDefaultICCProfileFilename) == nullptr)
    {
        return FALSE;
    }

    if (!PathFileExists(iccpath))
    {
        UnloadDefaultICCProfile();
        return TRUE;
    }

    if (!DeleteFile(iccpath))
    {
        return FALSE;
    }

    UnloadDefaultICCProfile();
    return TRUE;
}

BOOL ImgItem::IsCMYKICCProfile(const std::wstring& filepath)
{
    try
    {
        FileMapView iccfilemap(filepath, FileMapView::Mode::Read);
        const auto profile = ColorProfile::OpenFromMemory(iccfilemap.data(), iccfilemap.filesize().LowPart);
        if (!profile.IsValid())
        {
            return FALSE;
        }

        return profile.IsCmyk();
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL ImgItem::TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer)
{
    if (!iccprofile_.IsValid())
    {
        LoadDefaultICCProfile();
    }

    ColorTransformResult result;
    if (iccprofile_.IsValid())
    {
        result =
            ColorTransform::TransformCmyk8ReversedToBgr8(iccprofile_, width, height, stride, newstride, buffer, heap_);
    }
    else
    {
        EnterCriticalSection(&DefaultICCProfileCriticalSection);
        cmykprofilesource_ = DefaultICCProfileSource;
        result = ColorTransform::TransformCmyk8ReversedToBgr8(DefaultICCProfile, width, height, stride, newstride,
                                                              buffer, heap_);
        LeaveCriticalSection(&DefaultICCProfileCriticalSection);
    }

    if (!result.Succeeded())
    {
        iccprofileloadfailed_ = TRUE;
        return FALSE;
    }

    return TRUE;
}

void ImgItem::CloseICCProfile()
{
    iccprofile_.Reset();
}

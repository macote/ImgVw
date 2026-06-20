#include "ImgItem.h"
#include "resource.h"

#include <limits>

cmsHPROFILE ImgItem::DefaultICCProfile = nullptr;

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
    iccprofile_ = cmsOpenProfileFromMem(iccprofiledata, iccprofiledatabytecount);
    if (iccprofile_ != nullptr && cmsGetColorSpace(iccprofile_) != cmsSigCmykData)
    {
        CloseICCProfile();
    }
    else if (iccprofile_ != nullptr)
    {
        cmykprofilesource_ = CmykProfileSource::Embedded;
    }
}

void ImgItem::LoadDefaultICCProfile()
{
    EnterCriticalSection(&DefaultICCProfileCriticalSection);
    if (DefaultICCProfile == nullptr)
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
                        DefaultICCProfile = cmsOpenProfileFromMem(iccfilemap.data(), iccfilemap.filesize().LowPart);
                    }
                }

                if (DefaultICCProfile != nullptr && cmsGetColorSpace(DefaultICCProfile) != cmsSigCmykData)
                {
                    cmsCloseProfile(DefaultICCProfile);
                    DefaultICCProfile = nullptr;
                }
                else if (DefaultICCProfile != nullptr)
                {
                    DefaultICCProfileSource = CmykProfileSource::UserDefault;
                }
            }
        }
        catch (...)
        {
            DefaultICCProfile = nullptr;
        }

        if (DefaultICCProfile == nullptr)
        {
            const auto resource = FindResource(nullptr, MAKEINTRESOURCE(IDR_DEFAULT_CMYK_ICC), RT_RCDATA);
            const auto resource_size = resource == nullptr ? 0 : SizeofResource(nullptr, resource);
            const auto resource_handle = resource == nullptr ? nullptr : LoadResource(nullptr, resource);
            const auto resource_data = resource_handle == nullptr ? nullptr : LockResource(resource_handle);
            if (resource_data != nullptr && resource_size > 0)
            {
                DefaultICCProfile = cmsOpenProfileFromMem(resource_data, resource_size);
                if (DefaultICCProfile != nullptr && cmsGetColorSpace(DefaultICCProfile) != cmsSigCmykData)
                {
                    cmsCloseProfile(DefaultICCProfile);
                    DefaultICCProfile = nullptr;
                }
                else if (DefaultICCProfile != nullptr)
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
    if (DefaultICCProfile != nullptr)
    {
        cmsCloseProfile(DefaultICCProfile);
        DefaultICCProfile = nullptr;
    }
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
        const auto profile = cmsOpenProfileFromMem(iccfilemap.data(), iccfilemap.filesize().LowPart);
        if (profile == nullptr)
        {
            return FALSE;
        }

        const auto is_cmyk = cmsGetColorSpace(profile) == cmsSigCmykData;
        cmsCloseProfile(profile);

        return is_cmyk;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL ImgItem::TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer)
{
    if (iccprofile_ == nullptr)
    {
        LoadDefaultICCProfile();
    }

    // TODO: determine if other types of transform (TYPE_CMYK_8_REV => TYPE_BGR_8) should be supported.
    auto srgbprofile = cmsCreate_sRGBProfile();
    if (srgbprofile == nullptr)
    {
        iccprofileloadfailed_ = TRUE;
        return FALSE;
    }

    cmsHTRANSFORM transform{};
    if (iccprofile_ != nullptr)
    {
        transform = cmsCreateTransform(iccprofile_, TYPE_CMYK_8_REV, srgbprofile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
    }
    else
    {
        EnterCriticalSection(&DefaultICCProfileCriticalSection);
        cmykprofilesource_ = DefaultICCProfileSource;
        if (DefaultICCProfile != nullptr)
        {
            transform =
                cmsCreateTransform(DefaultICCProfile, TYPE_CMYK_8_REV, srgbprofile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
        }
        LeaveCriticalSection(&DefaultICCProfileCriticalSection);
    }

    if (transform == nullptr)
    {
        iccprofileloadfailed_ = TRUE;
        cmsCloseProfile(srgbprofile);

        return FALSE;
    }

    if (newstride <= 0 || height <= 0 ||
        static_cast<std::size_t>(newstride) > (std::numeric_limits<std::size_t>::max)() / height)
    {
        iccprofileloadfailed_ = TRUE;
        cmsDeleteTransform(transform);
        cmsCloseProfile(srgbprofile);
        return FALSE;
    }

    const auto newbuffersize = static_cast<std::size_t>(newstride) * height;
    auto newbuffer = reinterpret_cast<PBYTE>(HeapAlloc(heap_, 0, newbuffersize));
    if (newbuffer == nullptr)
    {
        iccprofileloadfailed_ = TRUE;
        cmsDeleteTransform(transform);
        cmsCloseProfile(srgbprofile);
        return FALSE;
    }

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

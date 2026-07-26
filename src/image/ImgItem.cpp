#include "ImgItem.h"
#include "resource.h"

#include "ColorTransform.h"

ColorProfile ImgItem::DefaultICCProfile;

ImgItem::CmykProfileSource ImgItem::DefaultICCProfileSource = ImgItem::CmykProfileSource::None;

CriticalSection ImgItem::DefaultICCProfileCriticalSection;

ImgItem::ImgItem(std::wstring filepath, INT targetwidth, INT targetheight)
    : ImgItem(std::move(filepath), targetwidth, targetheight, CreateEventW, SetEvent, ResetEvent, 0)
{
}

#if defined(IMGVW_TESTING)
ImgItem::ImgItem(std::wstring filepath, INT targetwidth, INT targetheight, LoadedEventCreate createevent,
                 LoadedEventSignal signalevent, LoadedEventReset resetevent)
    : ImgItem(std::move(filepath), targetwidth, targetheight, createevent, signalevent, resetevent, 0)
{
}
#endif

ImgItem::ImgItem(std::wstring filepath, INT targetwidth, INT targetheight, LoadedEventCreate createevent,
                 LoadedEventSignal signalevent, LoadedEventReset resetevent, int)
    : filepath_(std::move(filepath)), targetwidth_(targetwidth), targetheight_(targetheight),
      loadedevent_((createevent == nullptr ? CreateEventW : createevent)(nullptr, TRUE, FALSE, nullptr)),
      loadedeventcreationerror_(loadedevent_.valid() ? ERROR_SUCCESS : GetLastError()),
      signalloadedevent_(signalevent == nullptr ? SetEvent : signalevent),
      resetloadedevent_(resetevent == nullptr ? ResetEvent : resetevent)
{
    heap_ = GetProcessHeap();
    if (!loadedevent_.valid())
    {
        loadcompletionresult_ = {LoadCompletionStatus::EventCreationFailed, loadedeventcreationerror_};
    }
}

ImgItem::DisplayFrame::DisplayFrame(ImgBuffer buffer, INT targetwidth, INT targetheight, BOOL topdownbitmap)
    : buffer_(std::move(buffer))
{
    bitmapinfo_.bmiHeader.biCompression = BI_RGB;
    bitmapinfo_.bmiHeader.biBitCount = 24;
    bitmapinfo_.bmiHeader.biWidth = buffer_.width();
    bitmapinfo_.bmiHeader.biHeight = topdownbitmap ? -buffer_.height() : buffer_.height();
    bitmapinfo_.bmiHeader.biPlanes = 1;
    bitmapinfo_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    offsetx_ = (targetwidth - buffer_.width()) / 2;
    offsety_ = (targetheight - buffer_.height()) / 2;
}

ImgBitmap ImgItem::DisplayFrame::GetBitmap() const
{
    const auto filemap = buffer_.GetFileMapView();
    return ImgBitmap(&bitmapinfo_, filemap.data(), buffer_.buffersize());
}

void ImgItem::Unload()
{
    {
        CriticalSectionLock lock(displaystatecriticalsection_);
        status_ = Status::Queued;
        displayframe_.reset();
        errormessage_.clear();
    }
    ResetLoadingProgress();
    iccprofileloadfailed_ = FALSE;
    cmykprofilesource_ = CmykProfileSource::None;
    CloseICCProfile();
    ResetLoadCompletion();
}

ImgItem::LoadCompletionResult ImgItem::loadcompletionresult() const
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    return loadcompletionresult_;
}

bool ImgItem::SignalLoadComplete()
{
    if (!loadedevent_.valid())
    {
        return false;
    }

    {
        CriticalSectionLock lock(displaystatecriticalsection_);
        loadcompletionresult_ = {LoadCompletionStatus::Signaled, ERROR_SUCCESS};
    }
    if (!signalloadedevent_(loadedevent_.get()))
    {
        const auto error = GetLastError();
        CriticalSectionLock lock(displaystatecriticalsection_);
        loadcompletionresult_ = {LoadCompletionStatus::SignalFailed, error};
        return false;
    }

    return true;
}

bool ImgItem::ResetLoadCompletion()
{
    if (!loadedevent_.valid())
    {
        return false;
    }
    if (!resetloadedevent_(loadedevent_.get()))
    {
        const auto error = GetLastError();
        CriticalSectionLock lock(displaystatecriticalsection_);
        loadcompletionresult_ = {LoadCompletionStatus::ResetFailed, error};
        return false;
    }

    CriticalSectionLock lock(displaystatecriticalsection_);
    loadcompletionresult_ = {LoadCompletionStatus::Pending, ERROR_SUCCESS};
    return true;
}

void ImgItem::SetupDisplayParameters(BOOL topdownbitmap)
{
    auto frame = std::make_shared<const DisplayFrame>(std::move(pending_displaybuffer_), targetwidth_, targetheight_,
                                                      topdownbitmap);
    CriticalSectionLock lock(displaystatecriticalsection_);
    displayframe_ = std::move(frame);
    errormessage_.clear();
    status_ = Status::Ready;
}

void ImgItem::SetStatus(Status status)
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    status_ = status;
    if (status != Status::Ready)
    {
        displayframe_.reset();
    }
    if (status == Status::Loading)
    {
        errormessage_.clear();
    }
}

void ImgItem::SetError(std::wstring errormessage)
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    displayframe_.reset();
    errormessage_ = std::move(errormessage);
    status_ = Status::Error;
}

ImgItem::DisplayState ImgItem::GetDisplayState() const
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    return DisplayState{status_, displayframe_, errormessage_};
}

ImgBitmap ImgItem::GetDisplayBitmap() const
{
    const auto state = GetDisplayState();
    if (state.status != Status::Ready || state.frame == nullptr)
    {
        throw std::runtime_error("ImgItem.GetDisplayBitmap() called without a ready display frame.");
    }

    return state.frame->GetBitmap();
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
    CriticalSectionLock lock(DefaultICCProfileCriticalSection);
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
}

void ImgItem::UnloadDefaultICCProfile()
{
    CriticalSectionLock lock(DefaultICCProfileCriticalSection);
    DefaultICCProfile.Reset();
    DefaultICCProfileSource = CmykProfileSource::None;
}

ImgItem::DefaultICCProfileResetResult ImgItem::ResetDefaultICCProfile()
{
    TCHAR appdatapath[MAX_PATH]{};
    const auto appdataresult = SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdatapath);
    if (FAILED(appdataresult))
    {
        return {DefaultICCProfileResetStatus::AppDataPathFailed, appdataresult};
    }

    TCHAR imgvwappdatapath[MAX_PATH]{};
    if (PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath) == nullptr)
    {
        return {DefaultICCProfileResetStatus::PathConstructionFailed, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)};
    }

    TCHAR iccpath[MAX_PATH]{};
    if (PathCombine(iccpath, imgvwappdatapath, kDefaultICCProfileFilename) == nullptr)
    {
        return {DefaultICCProfileResetStatus::PathConstructionFailed, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)};
    }

    if (!DeleteFile(iccpath))
    {
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        {
            return {DefaultICCProfileResetStatus::DeleteFailed, HRESULT_FROM_WIN32(error)};
        }
    }

    UnloadDefaultICCProfile();
    return {DefaultICCProfileResetStatus::Succeeded, S_OK};
}

ImgItem::CmykProfileValidationResult ImgItem::ValidateCMYKICCProfile(const std::wstring& filepath)
{
    try
    {
        FileMapView iccfilemap(filepath, FileMapView::Mode::Read);
        if (iccfilemap.filesize().HighPart != 0)
        {
            return {CmykProfileValidationStatus::FileSizeUnsupported, ERROR_FILE_TOO_LARGE};
        }

        const auto profile = ColorProfile::OpenFromMemory(iccfilemap.data(), iccfilemap.filesize().LowPart);
        if (!profile.IsValid())
        {
            return {CmykProfileValidationStatus::InvalidProfile, ERROR_SUCCESS};
        }

        if (!profile.IsCmyk())
        {
            return {CmykProfileValidationStatus::WrongColorSpace, ERROR_SUCCESS};
        }

        return {CmykProfileValidationStatus::Valid, ERROR_SUCCESS};
    }
    catch (const FileMapError& error)
    {
        return {CmykProfileValidationStatus::FileAccessFailed, error.win32_error()};
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
        CriticalSectionLock lock(DefaultICCProfileCriticalSection);
        cmykprofilesource_ = DefaultICCProfileSource;
        result = ColorTransform::TransformCmyk8ReversedToBgr8(DefaultICCProfile, width, height, stride, newstride,
                                                              buffer, heap_);
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

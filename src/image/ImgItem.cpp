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
        imagewidth_ = 0;
        imageheight_ = 0;
        hasfilesize_ = FALSE;
        filesize_ = 0;
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
        imagewidth_ = 0;
        imageheight_ = 0;
        hasfilesize_ = FALSE;
        filesize_ = 0;
    }
}

void ImgItem::SetError(std::wstring errormessage)
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    displayframe_.reset();
    errormessage_ = std::move(errormessage);
    status_ = Status::Error;
}

void ImgItem::SetImageDimensions(INT width, INT height)
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    imagewidth_ = width;
    imageheight_ = height;
}

void ImgItem::SetFileSize(ULONGLONG filesize)
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    hasfilesize_ = TRUE;
    filesize_ = filesize;
}

ImgItem::DisplayState ImgItem::GetDisplayState() const
{
    CriticalSectionLock lock(displaystatecriticalsection_);
    return DisplayState{status_, displayframe_, errormessage_, imagewidth_, imageheight_, hasfilesize_, filesize_};
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

ImgItem::DefaultICCProfileLoadResult ImgItem::LoadDefaultICCProfile()
{
    CriticalSectionLock lock(DefaultICCProfileCriticalSection);
    if (DefaultICCProfile.IsValid())
    {
        return {DefaultICCProfileLoadStatus::AlreadyLoaded, ERROR_SUCCESS};
    }

    TCHAR appdatapath[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdatapath)))
    {
        TCHAR imgvwappdatapath[MAX_PATH]{};
        TCHAR iccpath[MAX_PATH]{};
        if (PathCombine(imgvwappdatapath, appdatapath, ImgSettings::kAppDataPath) != nullptr &&
            PathCombine(iccpath, imgvwappdatapath, kDefaultICCProfileFilename) != nullptr && PathFileExists(iccpath))
        {
            const auto validation = ValidateCMYKICCProfile(iccpath);
            if (validation.Succeeded())
            {
                FileMapView iccfilemap;
                if (iccfilemap.TryOpen(iccpath, FileMapView::Mode::Read).Succeeded())
                {
                    DefaultICCProfile = ColorProfile::OpenFromMemory(iccfilemap.data(), iccfilemap.filesize().LowPart);
                }

                if (DefaultICCProfile.IsValid())
                {
                    DefaultICCProfileSource = CmykProfileSource::UserDefault;
                    return {DefaultICCProfileLoadStatus::UserDefaultLoaded, ERROR_SUCCESS};
                }
            }
        }
    }

    const auto resource = FindResource(nullptr, MAKEINTRESOURCE(IDR_DEFAULT_CMYK_ICC), RT_RCDATA);
    if (resource == nullptr)
    {
        return {DefaultICCProfileLoadStatus::BundledResourceUnavailable, GetLastError()};
    }

    const auto resource_size = SizeofResource(nullptr, resource);
    const auto resource_handle = LoadResource(nullptr, resource);
    const auto resource_data = resource_handle == nullptr ? nullptr : LockResource(resource_handle);
    if (resource_data == nullptr || resource_size == 0)
    {
        return {DefaultICCProfileLoadStatus::BundledResourceUnavailable,
                resource_data == nullptr ? GetLastError() : ERROR_RESOURCE_DATA_NOT_FOUND};
    }

    DefaultICCProfile = ColorProfile::OpenFromMemory(resource_data, resource_size);
    if (!DefaultICCProfile.IsValid())
    {
        return {DefaultICCProfileLoadStatus::BundledProfileInvalid, ERROR_SUCCESS};
    }
    if (!DefaultICCProfile.IsCmyk())
    {
        DefaultICCProfile.Reset();
        return {DefaultICCProfileLoadStatus::BundledProfileWrongColorSpace, ERROR_SUCCESS};
    }

    DefaultICCProfileSource = CmykProfileSource::BundledFallback;
    return {DefaultICCProfileLoadStatus::BundledFallbackLoaded, ERROR_SUCCESS};
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
    FileMapView iccfilemap;
    const auto map_result = iccfilemap.TryOpen(filepath, FileMapView::Mode::Read);
    if (!map_result.Succeeded())
    {
        return {CmykProfileValidationStatus::FileAccessFailed, map_result.win32_error};
    }
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

BOOL ImgItem::TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer)
{
    if (!iccprofile_.IsValid())
    {
        static_cast<void>(LoadDefaultICCProfile());
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

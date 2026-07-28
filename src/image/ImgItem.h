#pragma once

#include "ColorProfile.h"
#include "CriticalSection.h"
#include "ImgBitmap.h"
#include "ImgBuffer.h"
#include "ImgSettings.h"
#include "FileMapView.h"
#include "Win32Handle.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <string>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

class ImgItem
{
  public:
    static constexpr auto kResizePercentThreshold = 0.025l;
    static constexpr auto kDefaultICCProfileFilename = L"default.icc";

  public:
    enum class Format
    {
        JPEG,
        PNG,
        HEIF,
        Other,
        Unsupported
    };
    enum class Status
    {
        Queued,
        Loading,
        Ready,
        Error
    };
    enum class CmykProfileSource
    {
        None,
        Embedded,
        UserDefault,
        BundledFallback
    };
    enum class CmykProfileValidationStatus
    {
        Valid,
        FileAccessFailed,
        FileSizeUnsupported,
        InvalidProfile,
        WrongColorSpace
    };
    enum class LoadCompletionStatus
    {
        Pending,
        Signaled,
        EventCreationFailed,
        ResetFailed,
        SignalFailed
    };
    struct LoadCompletionResult
    {
        LoadCompletionStatus status{LoadCompletionStatus::Pending};
        DWORD win32_error{ERROR_SUCCESS};

        bool Succeeded() const
        {
            return status == LoadCompletionStatus::Signaled;
        }
    };
    struct CmykProfileValidationResult
    {
        CmykProfileValidationStatus status{CmykProfileValidationStatus::InvalidProfile};
        DWORD win32_error{ERROR_SUCCESS};

        bool Succeeded() const
        {
            return status == CmykProfileValidationStatus::Valid;
        }
    };
    enum class DefaultICCProfileLoadStatus
    {
        AlreadyLoaded,
        UserDefaultLoaded,
        BundledFallbackLoaded,
        BundledResourceUnavailable,
        BundledProfileInvalid,
        BundledProfileWrongColorSpace
    };
    struct DefaultICCProfileLoadResult
    {
        DefaultICCProfileLoadStatus status{DefaultICCProfileLoadStatus::BundledResourceUnavailable};
        DWORD win32_error{ERROR_SUCCESS};

        bool Succeeded() const
        {
            return status == DefaultICCProfileLoadStatus::AlreadyLoaded ||
                   status == DefaultICCProfileLoadStatus::UserDefaultLoaded ||
                   status == DefaultICCProfileLoadStatus::BundledFallbackLoaded;
        }
    };
    enum class DefaultICCProfileResetStatus
    {
        Succeeded,
        AppDataPathFailed,
        PathConstructionFailed,
        DeleteFailed
    };
    struct DefaultICCProfileResetResult
    {
        DefaultICCProfileResetStatus status{DefaultICCProfileResetStatus::PathConstructionFailed};
        HRESULT system_error{E_FAIL};

        bool Succeeded() const
        {
            return status == DefaultICCProfileResetStatus::Succeeded;
        }
    };

    class DisplayFrame final
    {
      public:
        DisplayFrame(ImgBuffer buffer, INT targetwidth, INT targetheight, BOOL topdownbitmap);
        DisplayFrame(const DisplayFrame&) = delete;
        DisplayFrame& operator=(const DisplayFrame&) = delete;
        INT width() const
        {
            return buffer_.width();
        }
        INT height() const
        {
            return buffer_.height();
        }
        INT offsetx() const
        {
            return offsetx_;
        }
        INT offsety() const
        {
            return offsety_;
        }
        DWORD buffersize() const
        {
            return buffer_.buffersize();
        }
        ImgBitmap GetBitmap() const;

      private:
        ImgBuffer buffer_;
        BITMAPINFO bitmapinfo_{};
        INT offsetx_{};
        INT offsety_{};
    };

    struct DisplayState
    {
        Status status{Status::Queued};
        std::shared_ptr<const DisplayFrame> frame;
        std::wstring errormessage;
        INT imagewidth{};
        INT imageheight{};
        BOOL hasfilesize{};
        ULONGLONG filesize{};
    };

  public:
    ImgItem(std::wstring filepath, INT targetwidth, INT targetheight);
    virtual ~ImgItem()
    {
        CloseICCProfile();
    }
    ImgItem(const ImgItem&) = delete;
    ImgItem& operator=(const ImgItem&) = delete;
    virtual void Load() = 0;
    virtual void Unload();
    Status status() const
    {
        return GetDisplayState().status;
    }
    BOOL iccprofileloadfailed() const
    {
        return iccprofileloadfailed_;
    }
    std::wstring errormessage() const
    {
        return GetDisplayState().errormessage;
    }
    INT displaywidth() const
    {
        const auto state = GetDisplayState();
        return state.frame == nullptr ? 0 : state.frame->width();
    }
    INT displayheight() const
    {
        const auto state = GetDisplayState();
        return state.frame == nullptr ? 0 : state.frame->height();
    }
    INT offsetx() const
    {
        const auto state = GetDisplayState();
        return state.frame == nullptr ? 0 : state.frame->offsetx();
    }
    INT offsety() const
    {
        const auto state = GetDisplayState();
        return state.frame == nullptr ? 0 : state.frame->offsety();
    }
    INT targetwidth() const
    {
        return targetwidth_;
    }
    INT targetheight() const
    {
        return targetheight_;
    }
    DWORD displaybuffersize() const
    {
        const auto state = GetDisplayState();
        return state.frame == nullptr ? 0 : state.frame->buffersize();
    }
    INT loadingprogresspercent() const
    {
        return static_cast<INT>(InterlockedCompareExchange(const_cast<volatile LONG*>(&loadingprogresspercent_), 0, 0));
    }
    BOOL supportsloadingprogress() const
    {
        return supportsloadingprogress_;
    }
    void ResetLoadingProgress()
    {
        InterlockedExchange(&loadingprogresspercent_, -1);
    }
    void SetLoadingProgressPercent(INT percent)
    {
        if (percent < 0)
        {
            percent = 0;
        }
        else if (percent > 100)
        {
            percent = 100;
        }

        InterlockedExchange(&loadingprogresspercent_, percent);
    }
    HANDLE loadedevent() const
    {
        return loadedevent_.get();
    }
    LoadCompletionResult loadcompletionresult() const;
    DisplayState GetDisplayState() const;
    ImgBitmap GetDisplayBitmap() const;
    static DefaultICCProfileLoadResult LoadDefaultICCProfile();
    static void UnloadDefaultICCProfile();
    static DefaultICCProfileResetResult ResetDefaultICCProfile();
    static CmykProfileValidationResult ValidateCMYKICCProfile(const std::wstring& filepath);
    CmykProfileSource cmykprofilesource() const
    {
        return cmykprofilesource_;
    }

  protected:
    using LoadedEventCreate = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
    using LoadedEventSignal = BOOL(WINAPI*)(HANDLE);
    using LoadedEventReset = BOOL(WINAPI*)(HANDLE);

#if defined(IMGVW_TESTING)
    ImgItem(std::wstring filepath, INT targetwidth, INT targetheight, LoadedEventCreate createevent,
            LoadedEventSignal signalevent, LoadedEventReset resetevent);
#endif

    class LoadCompletion final
    {
      public:
        explicit LoadCompletion(ImgItem& item) : item_(item) {}
        ~LoadCompletion()
        {
            item_.SignalLoadComplete();
        }
        LoadCompletion(const LoadCompletion&) = delete;
        LoadCompletion& operator=(const LoadCompletion&) = delete;

      private:
        ImgItem& item_;
    };

    std::wstring filepath_;
    INT targetwidth_;
    INT targetheight_;
    INT width_{};
    INT height_{};
    ImgBuffer pending_displaybuffer_;
    volatile LONG loadingprogresspercent_{-1};
    Win32Handle loadedevent_;
    DWORD loadedeventcreationerror_{ERROR_SUCCESS};
    HANDLE heap_{INVALID_HANDLE_VALUE};

  protected:
    void SetupDisplayParameters()
    {
        SetupDisplayParameters(FALSE);
    }
    void SetupDisplayParameters(BOOL topdownbitmap);
    void SetStatus(Status status);
    void SetError(std::wstring errormessage = {});
    void SetImageDimensions(INT width, INT height);
    void SetFileSize(ULONGLONG filesize);
    bool SignalLoadComplete();
    void OpenICCProfile(const BYTE* iccprofiledata, UINT iccprofiledatabytecount);
    BOOL IsICCProfileLoaded() const
    {
        return iccprofile_.IsValid();
    }
    BOOL TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer);
    void CloseICCProfile();
    void SetSupportsLoadingProgress(BOOL supportsloadingprogress)
    {
        supportsloadingprogress_ = supportsloadingprogress;
    }

  private:
    ImgItem(std::wstring filepath, INT targetwidth, INT targetheight, LoadedEventCreate createevent,
            LoadedEventSignal signalevent, LoadedEventReset resetevent, int);
    bool ResetLoadCompletion();
    static ColorProfile DefaultICCProfile;
    static CmykProfileSource DefaultICCProfileSource;
    static CriticalSection DefaultICCProfileCriticalSection;

  private:
    mutable CriticalSection displaystatecriticalsection_;
    Status status_{Status::Queued};
    std::shared_ptr<const DisplayFrame> displayframe_;
    std::wstring errormessage_;
    INT imagewidth_{};
    INT imageheight_{};
    BOOL hasfilesize_{};
    ULONGLONG filesize_{};
    ColorProfile iccprofile_;
    BOOL iccprofileloadfailed_{};
    CmykProfileSource cmykprofilesource_{CmykProfileSource::None};
    BOOL supportsloadingprogress_{FALSE};
    LoadedEventSignal signalloadedevent_{SetEvent};
    LoadedEventReset resetloadedevent_{ResetEvent};
    LoadCompletionResult loadcompletionresult_;
};

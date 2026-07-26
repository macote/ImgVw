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
    };

  public:
    ImgItem(std::wstring filepath, INT targetwidth, INT targetheight)
        : filepath_(filepath), targetwidth_(targetwidth), targetheight_(targetheight),
          loadedevent_(CreateEvent(nullptr, TRUE, FALSE, nullptr))
    {
        heap_ = GetProcessHeap();
    }
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
    DisplayState GetDisplayState() const;
    ImgBitmap GetDisplayBitmap() const;
    static void LoadDefaultICCProfile();
    static void UnloadDefaultICCProfile();
    static BOOL ResetDefaultICCProfile();
    static BOOL IsCMYKICCProfile(const std::wstring& filepath);
    CmykProfileSource cmykprofilesource() const
    {
        return cmykprofilesource_;
    }

  protected:
    std::wstring filepath_;
    INT targetwidth_;
    INT targetheight_;
    INT width_{};
    INT height_{};
    ImgBuffer pending_displaybuffer_;
    volatile LONG loadingprogresspercent_{-1};
    Win32Handle loadedevent_;
    HANDLE heap_{INVALID_HANDLE_VALUE};

  protected:
    void SetupDisplayParameters()
    {
        SetupDisplayParameters(FALSE);
    }
    void SetupDisplayParameters(BOOL topdownbitmap);
    void SetStatus(Status status);
    void SetError(std::wstring errormessage = {});
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
    static ColorProfile DefaultICCProfile;
    static CmykProfileSource DefaultICCProfileSource;
    static CriticalSection DefaultICCProfileCriticalSection;

  private:
    mutable CriticalSection displaystatecriticalsection_;
    Status status_{Status::Queued};
    std::shared_ptr<const DisplayFrame> displayframe_;
    std::wstring errormessage_;
    ColorProfile iccprofile_;
    BOOL iccprofileloadfailed_{};
    CmykProfileSource cmykprofilesource_{CmykProfileSource::None};
    BOOL supportsloadingprogress_{FALSE};
};

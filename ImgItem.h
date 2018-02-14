#pragma once

#include "ImgBitmap.h"
#include "ImgBuffer.h"
#include "FileMapView.h"
#include "3rd-party\Little-CMS\lcms2.h"
#include <Windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>

class ImgItem
{
public:
    static constexpr double kResizePercentThreshold = 0.025;
public:
    enum class Format
    {
        JPEG,
        PNG,
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
public:
    ImgItem(std::wstring filepath, INT targetwidth, INT targetheight)
        : filepath_(filepath), targetwidth_(targetwidth), targetheight_(targetheight)
    {
        loadedevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        heap_ = GetProcessHeap();
    }
    virtual ~ImgItem()
    {
        if (pbitmapinfo_ != nullptr)
        {
            HeapFree(heap_, 0, pbitmapinfo_);
        }

        CloseICCProfile();
        CloseHandle(loadedevent_);
    }
    ImgItem(const ImgItem&) = delete;
    ImgItem& operator=(const ImgItem&) = delete;
    virtual void Load() = 0;
    virtual void Unload();
    Status status() const { return status_; }
    std::wstring errormessage() const { return errormessage_; }
    INT displaywidth() const { return displaybuffer_.width(); }
    INT displayheight() const { return displaybuffer_.height(); }
    INT offsetx() const { return offsetx_; }
    INT offsety() const { return offsety_; }
    HANDLE loadedevent() const { return loadedevent_; }
    ImgBitmap GetDisplayBitmap() const;
protected:
    void SetupDisplayParameters()
    {
        SetupDisplayParameters(FALSE);
    }
    void SetupDisplayParameters(BOOL topdownbitmap);
    void OpenICCProfile(PBYTE iccprofiledata, UINT iccprofiledatabytecount);
    BOOL IsICCProfileLoaded() const { return iccprofile_ != nullptr; }
    void TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer) const;
    void CloseICCProfile();
protected:
    std::wstring filepath_;
    INT targetwidth_;
    INT targetheight_;
    INT width_{};
    INT height_{};
    INT offsetx_{};
    INT offsety_{};
    ImgBuffer displaybuffer_;
    Status status_{ Status::Queued };
    std::wstring errormessage_;
    HANDLE loadedevent_{ NULL };
    HANDLE heap_{ INVALID_HANDLE_VALUE };
private:
    PBITMAPINFO pbitmapinfo_{ nullptr };
    cmsHPROFILE iccprofile_{ nullptr };
};
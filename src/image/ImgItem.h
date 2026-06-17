#pragma once

#include "ImgBitmap.h"
#include "ImgBuffer.h"
#include "ImgSettings.h"
#include "FileMapView.h"
#include "3rd-party\Little-CMS\lcms2.h"
#include <Windows.h>
#include <Shlwapi.h>
#include <string>
#include <iomanip>
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
    BOOL iccprofileloadfailed() const { return iccprofileloadfailed_; }
    std::wstring errormessage() const { return errormessage_; }
    INT displaywidth() const { return displaybuffer_.width(); }
    INT displayheight() const { return displaybuffer_.height(); }
    INT offsetx() const { return offsetx_; }
    INT offsety() const { return offsety_; }
    HANDLE loadedevent() const { return loadedevent_; }
    ImgBitmap GetDisplayBitmap() const;
    static void LoadDefaultICCProfile();
    static void UnloadDefaultICCProfile();
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
protected:
    void SetupDisplayParameters() { SetupDisplayParameters(FALSE); }
    void SetupDisplayParameters(BOOL topdownbitmap);
    void OpenICCProfile(const PBYTE iccprofiledata, UINT iccprofiledatabytecount);
    BOOL IsICCProfileLoaded() const { return iccprofile_ != nullptr; }
    BOOL TranformCMYK8ColorsToBGR8(INT width, INT height, INT stride, INT newstride, PBYTE* buffer);
    void CloseICCProfile();
private:
    static cmsHPROFILE DefaultICCProfile;
    static CRITICAL_SECTION DefaultICCProfileCriticalSection;
    static struct DefaultICCProfileCriticalSectionInitializer
    {
        DefaultICCProfileCriticalSectionInitializer()
        {
            if (!InitializeCriticalSectionAndSpinCount(&DefaultICCProfileCriticalSection, 0x00000400))
            {
                // TODO: handle error
            }
        }
    } defaultICCProfileCriticalSectionInitializer;
private:
    PBITMAPINFO pbitmapinfo_{ nullptr };
    cmsHPROFILE iccprofile_{ nullptr };
    BOOL iccprofileloadfailed_{};
};
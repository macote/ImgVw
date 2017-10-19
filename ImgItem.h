#pragma once

#include "ImgBitmap.h"
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
    ImgItem(std::wstring filepath, std::wstring temppath, INT targetwidth, INT targetheight)
        : filepath_(filepath), targetwidth_(targetwidth), targetheight_(targetheight), temppath_(temppath)
    {
        loadedevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
        heap_ = GetProcessHeap();
        CreateTempFile();
    }
    virtual ~ImgItem()
    {
        if (pbitmapinfo_ != nullptr)
        {
            HeapFree(heap_, 0, pbitmapinfo_);
        }

        CloseHandle(loadedevent_);
        DeleteTempFile();
    }
    virtual void Load() = 0;
    virtual void Unload();
    Status status() const { return status_; }
    std::wstring errormessage() const { return errormessage_; }
    INT displaywidth() const { return displaywidth_; }
    INT displayheight() const { return displayheight_; }
    INT offsetx() const { return offsetx_; }
    INT offsety() const { return offsety_; }
    HANDLE loadedevent() const { return loadedevent_; }
    void HandleBuffer(INT width, INT height, INT stride, PBYTE buffer);
    ImgBitmap GetDisplayBitmap();
protected:
    void SetupRGBColorsBITMAPINFO(WORD bitCount, INT width, INT height);
    void CreateTempFile();
    void WriteTempFile(PBYTE buffer, INT buffersize);
    INT buffersize() const { return buffersize_; }
    INT stride() const { return stride_; }
    void OpenICCProfile(PBYTE iccprofiledata, UINT iccprofiledatabytecount);
    void TranformCMYK8ColorsToBGR8(PBYTE* buffer, UINT newstride);
    void CloseICCProfile();
protected:
    std::wstring filepath_;
    INT targetwidth_;
    INT targetheight_;
    INT displaywidth_{};
    INT displayheight_{};
    INT width_{};
    INT height_{};
    INT offsetx_{};
    INT offsety_{};
    INT buffersize_{};
    INT stride_{};
    Status status_{ Status::Queued };
    std::wstring errormessage_;
    HANDLE loadedevent_{ NULL };
    HANDLE heap_{ INVALID_HANDLE_VALUE };
    PBITMAPINFO pbitmapinfo_{ nullptr };
    cmsHPROFILE iccprofile_{ nullptr };
private:
    void DeleteTempFile();
private:
    std::wstring tempfilename_;
    std::wstring temppath_;
    HANDLE tempfile_{ INVALID_HANDLE_VALUE };
};
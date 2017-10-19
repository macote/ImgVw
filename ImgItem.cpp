#include "ImgItem.h"

void ImgItem::Unload()
{
    status_ = Status::Queued;
    ResetEvent(loadedevent_);
}

void ImgItem::SetupRGBColorsBITMAPINFO(WORD bitCount, INT width, INT height)
{
    pbitmapinfo_ = (PBITMAPINFO)HeapAlloc(heap_, 0, sizeof(BITMAPINFOHEADER));
    pbitmapinfo_->bmiHeader.biCompression = BI_RGB;
    pbitmapinfo_->bmiHeader.biBitCount = bitCount;
    pbitmapinfo_->bmiHeader.biWidth = width;
    pbitmapinfo_->bmiHeader.biHeight = height;
    pbitmapinfo_->bmiHeader.biPlanes = 1;
    pbitmapinfo_->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
}

void ImgItem::HandleBuffer(INT width, INT height, INT stride, PBYTE buffer)
{
    displaywidth_ = width;
    displayheight_ = height;
    buffersize_ = height * stride;
    WriteTempFile(buffer, buffersize_);
}

void ImgItem::CreateTempFile()
{
    TCHAR tempfilenamebuffer[MAX_PATH];
    if (!GetTempFileName(temppath_.c_str(), TEXT("ImgVw"), 0, tempfilenamebuffer))
    {
        // TODO: handle error.
    }

    tempfilename_ = tempfilenamebuffer;
    tempfile_ = CreateFile(tempfilename_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (tempfile_ == INVALID_HANDLE_VALUE)
    {
        std::stringstream ss;
        ss << "ImgItem.CreateTempFile(CreateFile()) failed with error ";
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase;
        ss << GetLastError();
        throw std::runtime_error(ss.str());
    }
}

void ImgItem::WriteTempFile(PBYTE buffer, INT buffersize)
{
    DWORD byteswritten;
    SetFilePointerEx(tempfile_, LARGE_INTEGER{ 0 }, NULL, FILE_BEGIN);
    if (!WriteFile(tempfile_, buffer, buffersize, &byteswritten, NULL))
    {
        std::stringstream ss;
        ss << "ImgItem.WriteTempFile(WriteFile()) failed with error ";
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase;
        ss << GetLastError();
        throw std::runtime_error(ss.str());
    }
}

void ImgItem::DeleteTempFile()
{
    if (tempfile_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(tempfile_);
    }

    if (tempfilename_.length() > 0)
    {
        DeleteFile(tempfilename_.c_str());
    }
}

ImgBitmap ImgItem::GetDisplayBitmap()
{
    FileMapView tempfileview(tempfile_, FileMapView::Mode::Read);
    return ImgBitmap(pbitmapinfo_, tempfileview.view(), buffersize_);
}

void ImgItem::OpenICCProfile(PBYTE iccprofiledata, UINT iccprofiledatabytecount)
{
    iccprofile_ = cmsOpenProfileFromMem(iccprofiledata, iccprofiledatabytecount);
}

void ImgItem::TranformCMYK8ColorsToBGR8(PBYTE* buffer, UINT newstride)
{
    // TODO: determine if other types of transform (TYPE_CMYK_8_REV => TYPE_BGR_8) should be supported.
    auto srgbprofile = cmsCreate_sRGBProfile();
    auto transform = cmsCreateTransform(iccprofile_, TYPE_CMYK_8_REV, srgbprofile, TYPE_BGR_8, INTENT_PERCEPTUAL, 0);
    auto newbuffer = (PBYTE)HeapAlloc(heap_, 0, buffersize_);
    cmsDoTransformLineStride(transform, *buffer, newbuffer, displaywidth_, displayheight_, stride_, newstride, 0, 0);

    HeapFree(heap_, 0, *buffer);
    *buffer = newbuffer;
    buffersize_ = newstride * displayheight_;

    cmsDeleteTransform(transform);
    cmsCloseProfile(srgbprofile);
}

void ImgItem::CloseICCProfile()
{
    if (iccprofile_ != nullptr)
    {
        cmsCloseProfile(iccprofile_);
    }
}

#include "ImgBuffer.h"

void ImgBuffer::CreateTempFile()
{
    if (tempfilename_.length() == 0)
    {
        TCHAR tempfilenamebuffer[MAX_PATH];
        if (!GetTempFileName(ImgSettings::GetInstance().temppath().c_str(), TEXT("ImgVw"), 0, tempfilenamebuffer))
        {
            // TODO: handle error.
        }

        tempfilename_ = tempfilenamebuffer;
    }

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

void ImgBuffer::WriteData(INT width, INT height, INT stride, const PBYTE buffer)
{
    if (tempfile_ == INVALID_HANDLE_VALUE)
    {
        CreateTempFile();
    }

    width_ = width;
    height_ = height;
    stride_ = stride;
    buffersize_ = stride * height;

    DWORD byteswritten{};
    SetFilePointerEx(tempfile_, LARGE_INTEGER{ 0 }, NULL, FILE_BEGIN);
    if (!WriteFile(tempfile_, buffer, buffersize_, &byteswritten, NULL))
    {
        std::stringstream ss;
        ss << "ImgItem.WriteTempFile(WriteFile()) failed with error ";
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase;
        ss << GetLastError();
        throw std::runtime_error(ss.str());
    }

    CloseTempFile();
}

FileMapView ImgBuffer::GetFileMapView() const 
{
    return FileMapView(tempfilename_, FileMapView::Mode::Read);
}

void ImgBuffer::CloseTempFile()
{
    if (tempfile_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(tempfile_);
        tempfile_ = INVALID_HANDLE_VALUE;
    }
}

void ImgBuffer::DeleteTempFile()
{
    CloseTempFile();

    if (tempfilename_.length() > 0)
    {
        DeleteFile(tempfilename_.c_str());
    }
}

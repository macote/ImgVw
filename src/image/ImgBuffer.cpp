#include "ImgBuffer.h"

#include <limits>

void ImgBuffer::CreateTempFile()
{
    if (tempfilename_.empty())
    {
        TCHAR tempfilenamebuffer[MAX_PATH]{};
        if (!GetTempFileName(ImgSettings::GetInstance().temppath().c_str(), TEXT("ImgVw"), 0, tempfilenamebuffer))
        {
            throw ImgBufferError(ImgBufferStatus::TempFileNameFailed, GetLastError());
        }

        tempfilename_ = tempfilenamebuffer;
    }

    tempfile_.reset(CreateFile(tempfilename_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!tempfile_.valid())
    {
        throw ImgBufferError(ImgBufferStatus::TempFileOpenFailed, GetLastError());
    }
}

void ImgBuffer::WriteData(INT width, INT height, INT stride, const BYTE* buffer)
{
    if (width <= 0 || height <= 0 || stride <= 0 || buffer == nullptr)
    {
        throw std::invalid_argument("ImgBuffer.WriteData() received invalid dimensions, stride, or buffer.");
    }
    if (width > ((std::numeric_limits<INT>::max)() - 3) / 3)
    {
        throw std::overflow_error("ImgBuffer.WriteData() row size exceeds the supported range.");
    }

    const auto minimumstride = ((width * 3) + 3) & ~3;
    if (stride < minimumstride)
    {
        throw std::invalid_argument("ImgBuffer.WriteData() stride is smaller than a padded 24-bpp row.");
    }

    const auto buffersize = static_cast<unsigned long long>(stride) * static_cast<unsigned long long>(height);
    if (buffersize > (std::numeric_limits<DWORD>::max)())
    {
        throw std::overflow_error("ImgBuffer.WriteData() buffer exceeds the Win32 file-write limit.");
    }

    if (!tempfile_.valid())
    {
        CreateTempFile();
    }

    const auto bytecount = static_cast<DWORD>(buffersize);
    LARGE_INTEGER beginning{};
    if (!SetFilePointerEx(tempfile_.get(), beginning, nullptr, FILE_BEGIN))
    {
        throw ImgBufferError(ImgBufferStatus::SeekFailed, GetLastError());
    }
    DWORD byteswritten{};
    if (!WriteFile(tempfile_.get(), buffer, bytecount, &byteswritten, nullptr))
    {
        throw ImgBufferError(ImgBufferStatus::WriteFailed, GetLastError());
    }
    if (byteswritten != bytecount)
    {
        throw ImgBufferError(ImgBufferStatus::WriteFailed, ERROR_WRITE_FAULT);
    }
    if (!SetEndOfFile(tempfile_.get()))
    {
        throw ImgBufferError(ImgBufferStatus::ResizeFailed, GetLastError());
    }

    width_ = width;
    height_ = height;
    stride_ = stride;
    buffersize_ = bytecount;
    CloseTempFile();
}

FileMapView ImgBuffer::GetFileMapView() const
{
    return FileMapView(tempfilename_, FileMapView::Mode::Read);
}

void ImgBuffer::CloseTempFile()
{
    tempfile_.reset();
}

void ImgBuffer::DeleteTempFile()
{
    CloseTempFile();

    if (!tempfilename_.empty())
    {
        DeleteFile(tempfilename_.c_str());
    }
}

#pragma once

#include "FileMapView.h"
#include "ImgSettings.h"
#include "Win32Handle.h"
#include <Windows.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

enum class ImgBufferStatus
{
    TempFileNameFailed,
    TempFileOpenFailed,
    SeekFailed,
    WriteFailed,
    ResizeFailed
};

class ImgBufferError final : public std::runtime_error
{
  public:
    ImgBufferError(ImgBufferStatus status, DWORD win32_error)
        : std::runtime_error(BuildMessage(status, win32_error)), status_(status), win32_error_(win32_error)
    {
    }

    ImgBufferStatus status() const
    {
        return status_;
    }

    DWORD win32_error() const
    {
        return win32_error_;
    }

  private:
    static std::string BuildMessage(ImgBufferStatus status, DWORD win32_error)
    {
        const char* operation = "Unknown";
        switch (status)
        {
        case ImgBufferStatus::TempFileNameFailed:
            operation = "GetTempFileName";
            break;
        case ImgBufferStatus::TempFileOpenFailed:
            operation = "CreateFile";
            break;
        case ImgBufferStatus::SeekFailed:
            operation = "SetFilePointerEx";
            break;
        case ImgBufferStatus::WriteFailed:
            operation = "WriteFile";
            break;
        case ImgBufferStatus::ResizeFailed:
            operation = "SetEndOfFile";
            break;
        }

        std::stringstream message;
        message << "ImgBuffer." << operation << "() failed with error 0x" << std::hex << std::setw(8)
                << std::setfill('0') << std::uppercase << win32_error;
        return message.str();
    }

    ImgBufferStatus status_;
    DWORD win32_error_;
};

class ImgBuffer
{
  public:
    ImgBuffer() {}
    ~ImgBuffer()
    {
        DeleteTempFile();
    }
    ImgBuffer(const ImgBuffer&) = delete;
    ImgBuffer(ImgBuffer&& other)
    {
        *this = std::move(other);
    }
    ImgBuffer& operator=(ImgBuffer&& other)
    {
        if (this != &other)
        {
            DeleteTempFile();

            width_ = other.width_;
            height_ = other.height_;
            stride_ = other.stride_;
            buffersize_ = other.buffersize_;
            other.buffersize_ = 0;
            tempfilename_ = std::move(other.tempfilename_);
            tempfile_ = std::move(other.tempfile_);
        }

        return *this;
    }
    void WriteData(INT width, INT height, INT stride, const BYTE* buffer);
    FileMapView GetFileMapView() const;
    INT width() const
    {
        return width_;
    }
    INT height() const
    {
        return height_;
    }
    INT stride() const
    {
        return stride_;
    }
    DWORD buffersize() const
    {
        return buffersize_;
    }

  private:
    INT width_{};
    INT height_{};
    INT stride_{};
    DWORD buffersize_{};
    std::wstring tempfilename_;
    Win32Handle tempfile_;

  private:
    void CreateTempFile();
    void CloseTempFile();
    void DeleteTempFile();
};

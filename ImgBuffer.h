#pragma once

#include "ImgSettings.h"
#include "FileMapView.h"
#include <Windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>

class ImgBuffer
{
public:
    ImgBuffer() { }
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
            buffer_ = other.buffer_;
            other.buffer_ = nullptr;
            buffersize_ = other.buffersize_;
            other.buffersize_ = 0;
            tempfilename_ = std::move(other.tempfilename_);
            tempfile_ = other.tempfile_;
            other.tempfile_ = INVALID_HANDLE_VALUE;
        }

        return *this;
    }
    void WriteData(INT width, INT height, INT stride, const PBYTE buffer);
    FileMapView GetFileMapView() const;
    INT width() const { return width_; }
    INT height() const { return height_; }
    INT stride() const { return stride_; }
    DWORD buffersize() const { return buffersize_; }
private:
    void CreateTempFile();
    void CloseTempFile();
    void DeleteTempFile();
private:
    INT width_{};
    INT height_{};
    INT stride_{};
    PBYTE buffer_{ nullptr };
    DWORD buffersize_{};
    std::wstring tempfilename_;
    HANDLE heap_{ INVALID_HANDLE_VALUE };
    HANDLE tempfile_{ INVALID_HANDLE_VALUE };
};
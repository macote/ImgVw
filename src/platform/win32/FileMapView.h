#pragma once

#include "Win32Handle.h"
#include <Windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>

enum class FileMapStatus
{
    OpenFailed,
    SizeFailed,
    MappingFailed,
    ViewFailed
};

class FileMapError final : public std::runtime_error
{
  public:
    FileMapError(FileMapStatus status, DWORD win32_error)
        : std::runtime_error(BuildMessage(status, win32_error)), status_(status), win32_error_(win32_error)
    {
    }

    FileMapStatus status() const
    {
        return status_;
    }

    DWORD win32_error() const
    {
        return win32_error_;
    }

  private:
    static std::string BuildMessage(FileMapStatus status, DWORD win32_error)
    {
        const char* operation = "Unknown";
        switch (status)
        {
        case FileMapStatus::OpenFailed:
            operation = "CreateFile";
            break;
        case FileMapStatus::SizeFailed:
            operation = "GetFileSizeEx";
            break;
        case FileMapStatus::MappingFailed:
            operation = "CreateFileMapping";
            break;
        case FileMapStatus::ViewFailed:
            operation = "MapViewOfFile";
            break;
        }

        std::stringstream message;
        message << "FileMapView." << operation << "() failed with error 0x" << std::hex << std::setw(8)
                << std::setfill('0') << std::uppercase << win32_error;
        return message.str();
    }

    FileMapStatus status_;
    DWORD win32_error_;
};

class FileMapView final
{
  public:
    enum class Mode
    {
        Read,
        WriteNew
    };

  public:
    FileMapView(const std::wstring& filepath, Mode mode) : filepath_(filepath), mode_(mode)
    {
        OpenFile();
        InitializeMapping();
    }
    ~FileMapView()
    {
        Close();
    }
    FileMapView(const FileMapView&) = delete;
    FileMapView(FileMapView&& other)
    {
        *this = std::move(other);
    }
    FileMapView& operator=(FileMapView&& other)
    {
        if (this != &other)
        {
            Close();

            filepath_ = std::move(other.filepath_);
            mode_ = other.mode_;
            file_ = std::move(other.file_);
            filesize_.QuadPart = other.filesize_.QuadPart;
            other.filesize_.QuadPart = 0;
            mapfile_ = std::move(other.mapfile_);
            data_ = other.data_;
            other.data_ = nullptr;
        }

        return *this;
    }
    LARGE_INTEGER filesize() const
    {
        return filesize_;
    }
    PBYTE data() const
    {
        return data_;
    }
    void Open(const std::wstring& filepath, Mode mode);
    void Close();

  private:
    std::wstring filepath_;
    Mode mode_{Mode::Read};
    Win32Handle file_;
    LARGE_INTEGER filesize_{0};
    Win32Handle mapfile_;
    PBYTE data_{nullptr};

  private:
    void InitializeMapping();
    void OpenFile();
    void GetFileSize();
    void OpenMapping();
    void MapView();
};

inline void FileMapView::Open(const std::wstring& filepath, Mode mode)
{
    if (file_.valid())
    {
        Close();
    }

    filepath_ = filepath;
    mode_ = mode;
    OpenFile();
    InitializeMapping();
}

inline void FileMapView::Close()
{
    if (data_ != nullptr)
    {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }

    mapfile_.reset();
    file_.reset();
}

inline void FileMapView::InitializeMapping()
{
    try
    {
        GetFileSize();
        OpenMapping();
        MapView();
    }
    catch (const std::exception&)
    {
        Close();
        throw;
    }
}

inline void FileMapView::OpenFile()
{
    if (mode_ == Mode::Read)
    {
        file_.reset(CreateFile(filepath_.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL));
    }
    else if (mode_ == Mode::WriteNew)
    {
        file_.reset(CreateFile(filepath_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL));
    }

    if (!file_.valid())
    {
        const auto error = GetLastError();
        throw FileMapError(FileMapStatus::OpenFailed, error);
    }
}

inline void FileMapView::GetFileSize()
{
    if (!GetFileSizeEx(file_.get(), &filesize_))
    {
        const auto error = GetLastError();
        throw FileMapError(FileMapStatus::SizeFailed, error);
    }
}

inline void FileMapView::OpenMapping()
{
    const auto flProtect = mode_ == Mode::Read ? PAGE_READONLY : PAGE_READWRITE;
    mapfile_.reset(CreateFileMapping(file_.get(), NULL, flProtect, 0, 0, NULL));
    if (!mapfile_.valid())
    {
        const auto error = GetLastError();
        throw FileMapError(FileMapStatus::MappingFailed, error);
    }
}

inline void FileMapView::MapView()
{
    const auto desiredAccess = mode_ == Mode::Read ? FILE_MAP_READ : FILE_MAP_WRITE;
    data_ = reinterpret_cast<PBYTE>(MapViewOfFile(mapfile_.get(), desiredAccess, 0, 0, 0));
    if (data_ == nullptr)
    {
        const auto error = GetLastError();
        throw FileMapError(FileMapStatus::ViewFailed, error);
    }
}

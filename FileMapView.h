#pragma once

#include <Windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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
            file_ = other.file_;
            other.file_ = INVALID_HANDLE_VALUE;
            filesize_.QuadPart = other.filesize_.QuadPart;
            other.filesize_.QuadPart = 0;
            mapfile_ = other.mapfile_;
            other.mapfile_ = INVALID_HANDLE_VALUE;
            data_ = other.data_;
            other.data_ = nullptr;
        }

        return *this;
    }
    LARGE_INTEGER filesize() const { return filesize_; }
    PBYTE data() const { return data_; }
    void Open(const std::wstring& filepath, Mode mode);
    void Close();
private:
    void InitializeMapping();
    void OpenFile();
    void GetFileSize();
    void OpenMapping();
    void MapView();
    std::wstring filepath_;
    Mode mode_{ Mode::Read };
    HANDLE file_{ INVALID_HANDLE_VALUE };
    LARGE_INTEGER filesize_{ 0 };
    HANDLE mapfile_{ INVALID_HANDLE_VALUE };
    PBYTE data_{ nullptr };
};

inline void FileMapView::Open(const std::wstring& filepath, Mode mode)
{
    if (file_ != INVALID_HANDLE_VALUE)
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

    if (mapfile_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(mapfile_);
        mapfile_ = INVALID_HANDLE_VALUE;
    }

    if (file_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
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
        file_ = CreateFile(filepath_.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    else if (mode_ == Mode::WriteNew)
    {
        file_ = CreateFile(filepath_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    if (file_ == INVALID_HANDLE_VALUE)
    {
        std::stringstream ss;
        ss << "FileMapView.OpenFile(CreateFile()) failed with error ";
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase;
        ss << GetLastError();
        throw std::runtime_error(ss.str());
    }
}

inline void FileMapView::GetFileSize()
{
    GetFileSizeEx(file_, &filesize_);
}

inline void FileMapView::OpenMapping()
{
    const auto flProtect = mode_ == Mode::Read ? PAGE_READONLY : PAGE_READWRITE;
    mapfile_ = CreateFileMapping(file_, NULL, flProtect, 0, 0, NULL);
    if (mapfile_ == NULL)
    {
        std::stringstream ss;
        ss << "FileMapView.OpenMapping(CreateFileMapping()) failed with error ";
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase;
        ss << GetLastError();
        throw std::runtime_error(ss.str());
    }
}

inline void FileMapView::MapView()
{
    const auto desiredAccess = mode_ == Mode::Read ? FILE_MAP_READ : FILE_MAP_WRITE;
    data_ = reinterpret_cast<PBYTE>(MapViewOfFile(mapfile_, desiredAccess, 0, 0, 0));
}
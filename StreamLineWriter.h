#pragma once

#include "FileStream.h"
#include <string>
#include <Windows.h>

class StreamLineWriter
{
public:
    enum class Encoding
    {
        UTF8
    };
    StreamLineWriter(std::wstring filepath, bool append) : StreamLineWriter(filepath, Encoding::UTF8, append)
    {
    }
    StreamLineWriter(std::wstring filepath, Encoding encoding, bool append)
        : filestream_(FileStream(filepath, append ? FileStream::Mode::Append : FileStream::Mode::Truncate)), encoding_(encoding)
    {
    }
    StreamLineWriter(StreamLineWriter&& other)
        : filestream_(std::move(other.filestream_)), encoding_(other.encoding_)
    {
    }
    StreamLineWriter& operator=(StreamLineWriter&& other);
    ~StreamLineWriter()
    {
        Close();
    }
    BOOL autoflush() const { return autoflush_; };
    void set_autoflush(const BOOL autoflush) { autoflush_ = autoflush; };
    void Write(std::wstring line);
    void WriteLine(std::wstring line);
    void Close()
    {
        filestream_.Close();
    }
private:
    void WriteEOL()
    {
        filestream_.Write((PBYTE)"\r\n", 2);
    }
    FileStream filestream_;
    Encoding encoding_;
    BOOL autoflush_{};
};

inline StreamLineWriter& StreamLineWriter::operator=(StreamLineWriter&& other)
{
    if (this != &other)
    {
        filestream_ = std::move(other.filestream_);
        encoding_ = other.encoding_;
    }

    return *this;
}

inline void StreamLineWriter::Write(std::wstring line)
{
    if (line.size() > 0)
    {
        if (encoding_ == Encoding::UTF8)
        {
            DWORD cbMultiByte = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
            LPSTR bytes = (LPSTR)HeapAlloc(GetProcessHeap(), 0, cbMultiByte);
            cbMultiByte = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, bytes, cbMultiByte, NULL, NULL);
            filestream_.Write((PBYTE)bytes, cbMultiByte - 1);
            HeapFree(GetProcessHeap(), 0, bytes);
        }
        else
        {
            throw std::runtime_error("StreamLineWriter::Write() error: the selected encoding is not supported.");
        }
    }
}

inline void StreamLineWriter::WriteLine(std::wstring line)
{
    Write(line);
    WriteEOL();
    if (autoflush_)
    {
        filestream_.Flush();
    }
}
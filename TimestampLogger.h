#pragma once

#include "StreamLineWriter.h"
#include <Windows.h>
#include <string>

class TimestampLogger
{
public:
    TimestampLogger(const std::wstring& filepath)
        : TimestampLogger(filepath, FALSE) { }
    TimestampLogger(const std::wstring& filepath, BOOL autoflush)
        : streamlinewriter_(StreamLineWriter(filepath, TRUE))
    {
        pwritecriticalsection_ = new CRITICAL_SECTION();
        if (!InitializeCriticalSectionAndSpinCount(pwritecriticalsection_, 0x00000400))
        {
            // TODO: handle error
        }

        set_autoflush(autoflush);
    }
    ~TimestampLogger()
    {
        if (pwritecriticalsection_ != nullptr)
        {
            DeleteCriticalSection(pwritecriticalsection_);
            delete pwritecriticalsection_;
        }

        Close();
    }
    TimestampLogger(const TimestampLogger&) = delete;
    TimestampLogger& operator=(const TimestampLogger&) = delete;
    void WriteLine(const std::wstring& line);
    void Close()
    {
        streamlinewriter_.Close();
    }
    BOOL autoflush() const { return streamlinewriter_.autoflush(); };
    void set_autoflush(BOOL autoflush) { streamlinewriter_.set_autoflush(autoflush); };
    static std::wstring GetTimestampString();
    static std::wstring GetTimestampString(BOOL asvalidfilename);
private:
    StreamLineWriter streamlinewriter_;
    PCRITICAL_SECTION pwritecriticalsection_{ nullptr };
};

inline void TimestampLogger::WriteLine(const std::wstring& line)
{
    EnterCriticalSection(pwritecriticalsection_);

    SYSTEMTIME filetime;
    GetLocalTime(&filetime);
    std::wstringstream wss;
    wss << L"[" << GetTimestampString() << L"] ";
    streamlinewriter_.WriteLine(wss.str() + line);

    LeaveCriticalSection(pwritecriticalsection_);
}

inline std::wstring TimestampLogger::GetTimestampString()
{
    return GetTimestampString(FALSE);
}

inline std::wstring TimestampLogger::GetTimestampString(BOOL asvalidfilename)
{
    SYSTEMTIME filetime;
    GetLocalTime(&filetime);
    std::wstringstream wss;
    const auto timeseparator = asvalidfilename ? L"." : L":";
    wss << filetime.wYear << L"-";
    wss << std::setw(2) << std::setfill(L'0') << filetime.wMonth << L"-";
    wss << std::setw(2) << std::setfill(L'0') << filetime.wDay << L"T";
    wss << std::setw(2) << std::setfill(L'0') << filetime.wHour << timeseparator;
    wss << std::setw(2) << std::setfill(L'0') << filetime.wMinute << timeseparator;
    wss << std::setw(2) << std::setfill(L'0') << filetime.wSecond << L".";
    wss << std::setw(3) << std::setfill(L'0') << filetime.wMilliseconds;

    return wss.str();
}
#pragma once

#include "StreamLineWriter.h"
#include <Windows.h>
#include <string>

class TimestampLogger
{
public:
	TimestampLogger(std::wstring filepath)
		: TimestampLogger(filepath, FALSE)
	{
	}
	TimestampLogger(std::wstring filepath, BOOL autoflush)
		: streamlinewriter_(StreamLineWriter(filepath, TRUE))
	{
		if (!InitializeCriticalSectionAndSpinCount(&writecriticalsection_, 0x00000400))
		{
			// TODO: handle error
		}

		set_autoflush(autoflush);
	}
	~TimestampLogger()
	{ 
		DeleteCriticalSection(&writecriticalsection_);
		Close();
	}
	void WriteLine(std::wstring line);
	void Close()
	{
		streamlinewriter_.Close();
	}
	BOOL autoflush() const { return streamlinewriter_.autoflush(); };
	void set_autoflush(const BOOL autoflush) { streamlinewriter_.set_autoflush(autoflush); };
private:
	StreamLineWriter streamlinewriter_;
	CRITICAL_SECTION writecriticalsection_;
};

inline void TimestampLogger::WriteLine(std::wstring line)
{
	EnterCriticalSection(&writecriticalsection_);

	SYSTEMTIME filetime;
	GetLocalTime(&filetime);
	std::wstringstream wss;
	wss << L"[" << filetime.wYear << L"-";
	wss << std::setw(2) << std::setfill(L'0') << filetime.wMonth << L"-";
	wss << std::setw(2) << std::setfill(L'0') << filetime.wDay << L" ";
	wss << std::setw(2) << std::setfill(L'0') << filetime.wHour << L":";
	wss << std::setw(2) << std::setfill(L'0') << filetime.wMinute << L":";
	wss << std::setw(2) << std::setfill(L'0') << filetime.wSecond << L".";
	wss << std::setw(3) << std::setfill(L'0') << filetime.wMilliseconds << L"] ";
	streamlinewriter_.WriteLine(wss.str() + line);

	LeaveCriticalSection(&writecriticalsection_);
}

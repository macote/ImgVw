#pragma once

#include <Windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>

class FileMapView
{
public:
	enum class Mode
	{
		Read,
		WriteNew
	};
public:
	FileMapView(std::wstring filepath, Mode mode) : filepath_(filepath), mode_(mode)
	{
		OpenFile();
		InitializeMapping();
	}
	FileMapView(HANDLE file, Mode mode) : mode_(mode), file_(file)
	{
		InitializeMapping();
	}
	~FileMapView()
	{
		CloseFileMapView();
	}
	LARGE_INTEGER filesize() const { return filesize_; }
	PBYTE view() const { return view_; }
private:
	void InitializeMapping();
	void OpenFile();
	void GetFileSize();
	void OpenMapping();
	void MapView();
	void CloseFileMapView();
	BOOL ownsfilehandle_{ FALSE };
	std::wstring filepath_;
	Mode mode_;
	HANDLE file_{ INVALID_HANDLE_VALUE };
	LARGE_INTEGER filesize_{ 0 };
	HANDLE mapfile_{ INVALID_HANDLE_VALUE };
	PBYTE view_{ nullptr };
};

inline void FileMapView::InitializeMapping()
{
	GetFileSize();
	OpenMapping();
	MapView();
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

	ownsfilehandle_ = TRUE;
}

inline void FileMapView::GetFileSize()
{
	GetFileSizeEx(file_, &filesize_);
}

inline void FileMapView::OpenMapping()
{
	auto flProtect = mode_ == Mode::Read ? PAGE_READONLY : PAGE_READWRITE;
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
	auto desiredAccess = mode_ == Mode::Read ? FILE_MAP_READ : FILE_MAP_WRITE;
	view_ = (PBYTE)MapViewOfFile(mapfile_, desiredAccess, 0, 0, 0);
}

inline void  FileMapView::CloseFileMapView()
{
	if (view_ != nullptr)
	{
		UnmapViewOfFile(view_);
	}

	if (mapfile_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle(mapfile_);
	}

	if (file_ != INVALID_HANDLE_VALUE && ownsfilehandle_)
	{
		CloseHandle(file_);
	}
}
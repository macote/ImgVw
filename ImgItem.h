#pragma once

#include "ImgBitmap.h"
#include "FileMapView.h"
#include <Windows.h>
#include <string>
#include <iomanip>
#include <sstream>
#include <stdexcept>

class ImgItem
{
public:
	static constexpr double kResizePercentThreshold = 0.025;
public:
	enum class Format
	{
		JPEG,
		PNG,
		Other,
		Unsupported
	};
	enum class Status
	{
		Queued,
		Loading,
		Ready,
		Error
	};
public:
	ImgItem(std::wstring filepath, std::wstring temppath, INT targetwidth, INT targetheight)
		: filepath_(filepath), targetwidth_(targetwidth), targetheight_(targetheight), temppath_(temppath)
	{
		loadedevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
		heap_ = GetProcessHeap();
		CreateTempFile();
	}
	virtual ~ImgItem()
	{
		if (pbitmapinfo_ != nullptr)
		{
			HeapFree(heap_, 0, pbitmapinfo_);
		}

		CloseHandle(loadedevent_);
		DeleteTempFile();
	}
	virtual void Load() = 0;
	virtual void Unload();
	Status status() const { return status_; }
	std::wstring errormessage() const { return errormessage_; }
	INT displaywidth() const { return displaywidth_; }
	INT displayheight() const { return displayheight_; }
	INT offsetx() const { return offsetx_; }
	INT offsety() const { return offsety_; }
	HANDLE loadedevent() const { return loadedevent_; }
	ImgBitmap GetDisplayBitmap();
protected:
	void SetupRGBColorsBITMAPINFO(WORD bitCount, INT width, INT height);
	void CreateTempFile();
	void WriteTempFile(PBYTE buffer, INT buffersize);
	INT buffersize() const { return buffersize_; }
	INT stride() const { return stride_; }
protected:
	std::wstring filepath_;
	INT targetwidth_;
	INT targetheight_;
	INT displaywidth_{};
	INT displayheight_{};
	INT width_{};
	INT height_{};
	INT offsetx_{};
	INT offsety_{};
	INT buffersize_{};
	INT stride_{};
	Status status_{ Status::Queued };
	std::wstring errormessage_;
	HANDLE loadedevent_{ NULL };
	HANDLE heap_{ INVALID_HANDLE_VALUE };
	PBITMAPINFO pbitmapinfo_{ nullptr };
private:
	void DeleteTempFile();
private:
	std::wstring tempfilename_;
	std::wstring temppath_;
	HANDLE tempfile_{ INVALID_HANDLE_VALUE };
};

inline void ImgItem::Unload()
{
	status_ = Status::Queued;
	ResetEvent(loadedevent_);
}

inline void ImgItem::SetupRGBColorsBITMAPINFO(WORD bitCount, INT width, INT height)
{
	pbitmapinfo_ = (PBITMAPINFO)HeapAlloc(heap_, 0, sizeof(BITMAPINFOHEADER));
	pbitmapinfo_->bmiHeader.biCompression = BI_RGB;
	pbitmapinfo_->bmiHeader.biBitCount = bitCount;
	pbitmapinfo_->bmiHeader.biWidth = width;
	pbitmapinfo_->bmiHeader.biHeight = height;
	pbitmapinfo_->bmiHeader.biPlanes = 1;
	pbitmapinfo_->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
}

inline void ImgItem::CreateTempFile()
{
	TCHAR tempfilenamebuffer[MAX_PATH];
	if (!GetTempFileName(temppath_.c_str(), TEXT("ImgVw"), 0, tempfilenamebuffer))
	{
		// TODO: handle error.
	}

	tempfilename_ = tempfilenamebuffer;
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

inline void ImgItem::WriteTempFile(PBYTE buffer, INT buffersize)
{
	DWORD byteswritten;
	SetFilePointerEx(tempfile_, LARGE_INTEGER{ 0 }, NULL, FILE_BEGIN);
	if (!WriteFile(tempfile_, buffer, buffersize, &byteswritten, NULL))
	{
		std::stringstream ss;
		ss << "ImgItem.WriteTempFile(WriteFile()) failed with error ";
		ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::uppercase;
		ss << GetLastError();
		throw std::runtime_error(ss.str());
	}
}

inline void ImgItem::DeleteTempFile()
{
	if (tempfile_ != INVALID_HANDLE_VALUE)
	{
		CloseHandle(tempfile_);
	}

	if (tempfilename_.length() > 0)
	{
		DeleteFile(tempfilename_.c_str());
	}
}

inline ImgBitmap ImgItem::GetDisplayBitmap()
{
	FileMapView tempfileview(tempfile_, FileMapView::Mode::Read);
	return ImgBitmap(pbitmapinfo_, tempfileview.view(), buffersize_);
}
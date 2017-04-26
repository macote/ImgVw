#pragma once

#include "ImgCache.h"
#include "ImgLoader.h"
#include <Windows.h>
#include <set>
#include <iterator>

class ImgBrowser
{
public:
	ImgBrowser()
	{
		InitializeTempPath();
		cache_.set_temppath(temppath_);
		if (!InitializeCriticalSectionAndSpinCount(&browsecriticalsection_, 0x00000400))
		{
			// TODO: handle error
		}
	}
	~ImgBrowser()
	{
		StopCollecting();
		DeleteCriticalSection(&browsecriticalsection_);
		if (collectorthread_ != NULL)
		{
			CloseHandle(collectorthread_);
		}

		DeleteTempPath();
	}
	void StartBrowsingAsync(std::wstring path, INT targetwidth, INT targetheight);
	void StopBrowsing();
	std::wstring GetCurrentFilePath();
	ImgItem* GetCurrentItem();
	BOOL MoveToNext();
	BOOL MoveToPrevious();
private:
	ImgCache cache_;
	ImgLoader loader_;
	BOOL cancellationflag_{};
	std::wstring folderpath_;
	std::wstring temppath_;
	std::set<std::wstring>::iterator currentfileiterator_;
	std::set<std::wstring> files_;
	HANDLE collectorthread_{ NULL };
	CRITICAL_SECTION browsecriticalsection_;
	INT targetwidth_{};
	INT targetheight_{};
private:
	void InitializeTempPath();
	void DeleteTempPath();
	void CollectFile(std::wstring filepath);
	DWORD CollectFolder(std::wstring folderpath);
	void StopCollecting();
	static DWORD WINAPI StaticThreadCollect(void* browserinstance);
	void Reset();
	BOOL IsFileFormatSupported(LPCTSTR fileName);
};
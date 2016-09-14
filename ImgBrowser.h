#pragma once

#include "ImgCache.h"
#include "ImgLoader.h"
#include <Windows.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <string>
#include <set>
#include <iterator>

class ImgBrowser
{
public:
	ImgBrowser()
	{
		if (!InitializeCriticalSectionAndSpinCount(&navigatecriticalsection_, 0x00000400))
		{
			// TODO: handle error
		}
	}
	~ImgBrowser()
	{
		StopCollecting();
		DeleteCriticalSection(&navigatecriticalsection_);
		if (collectorthread_ != NULL)
		{
			CloseHandle(collectorthread_);
		}
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
	std::set<std::wstring>::iterator currentfileiterator_;
	std::set<std::wstring> files_;
	HANDLE collectorthread_{ NULL };
	CRITICAL_SECTION navigatecriticalsection_;
	INT targetwidth_{};
	INT targetheight_{};
private:
	void CollectFile(std::wstring filepath);
	DWORD CollectFolder(std::wstring folderpath);
	void StopCollecting();
	static DWORD WINAPI StaticThreadCollect(void* browserinstance);
	void Reset();
	BOOL IsFileTypeSupported(LPCTSTR fileName);
};

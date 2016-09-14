#pragma once

#include "ImgItem.h"
#include <Windows.h>
#include <string>
#include <functional>
#include <deque>
#include <vector>

class LoaderItem
{
public:
	LoaderItem(ImgItem* imgitem, std::function<void()> handler)
		: imgitem_(imgitem), loadcompleteevent_(handler)
	{
	}
	ImgItem* imgitem() const { return imgitem_; }
	HANDLE loaderitemthread() const { return loaderitemthread_; }
	void set_loaderitemthread(HANDLE loaderitemthread) { loaderitemthread_ = loaderitemthread; }
	void LoadComplete();
private:
	ImgItem* imgitem_;
	HANDLE loaderitemthread_;
	std::function<void()> loadcompleteevent_{ nullptr };
};

inline void LoaderItem::LoadComplete()
{
	if (loadcompleteevent_ != nullptr)
	{
		loadcompleteevent_();
	}
}

class ImgLoader
{
public:
	ImgLoader()
	{
#if _DEBUG && LOG2
		kLogger.WriteLine(L"Start");
#endif
		if (!InitializeCriticalSectionAndSpinCount(&queuecriticalsection_, 0x00000400))
		{
			// TODO: handle error
		}

		cancelevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
		workevent_ = CreateEvent(NULL, TRUE, FALSE, NULL);
		// TODO: increase once GDI+ gets replaced
		loaders_.set_maximumcount(1);
		StartLoop();
	}
	~ImgLoader()
	{
		StopLoading();
		DeleteCriticalSection(&queuecriticalsection_);
		CloseHandle(cancelevent_);
		CloseHandle(workevent_);
		if (loopthread_ != NULL)
		{
			CloseHandle(loopthread_);
		}
#if _DEBUG && LOG2
		kLogger.WriteLine(L"End");
#endif
	}
	void LoadAsync(ImgItem* imgitem);
	void LoadNextAsync(ImgItem* imgitem);
	void StopLoading();
private:
	void StartLoop();
	DWORD Loop();
	BOOL ItemPending();
	void QueueItem(ImgItem* imgitem, BOOL pushfront);
	ImgItem* GetNextItem();
	static DWORD WINAPI StaticThreadLoop(void* imgloaderinstance);
	static DWORD WINAPI StaticThreadLoad(void* imgiteminstance);
private:
	std::deque<ImgItem*> queue_;
	std::deque<std::unique_ptr<LoaderItem>> loaderitems_;
	HANDLE workevent_;
	HANDLE cancelevent_;
	HANDLE loopthread_{ NULL };
	BOOL cancellationflag_{};
	CountSemaphore loaders_;
	CRITICAL_SECTION queuecriticalsection_;
#if _DEBUG && LOG2
	static void ThreadLogLine(std::wstring line);
	static TimestampLogger kLogger;
#endif
};

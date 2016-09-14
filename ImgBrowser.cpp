#include "ImgBrowser.h"

void ImgBrowser::CollectFile(std::wstring filepath)
{
	EnterCriticalSection(&navigatecriticalsection_);
	files_.insert(filepath);
	cache_.Add(filepath, targetwidth_, targetheight_);
	loader_.LoadAsync(cache_.Get(filepath).get());
	if (currentfileiterator_ == files_.end())
	{
		currentfileiterator_ = files_.begin();
	}

	LeaveCriticalSection(&navigatecriticalsection_);
}

DWORD ImgBrowser::CollectFolder(std::wstring folderpath)
{
	WIN32_FIND_DATA findfiledata;
	HANDLE hFind;
	std::wstring pattern = folderpath + L"*";
	hFind = FindFirstFileW(pattern.c_str(), &findfiledata);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (lstrcmpW(findfiledata.cFileName, L".") != 0 && lstrcmpW(findfiledata.cFileName, L"..") != 0)
				{
					std::wstring currentpath(folderpath + findfiledata.cFileName + L"\\");
					// TODO: add support for recursivity
				}
			}
			else
			{
				if (IsFileTypeSupported(findfiledata.cFileName))
				{
					std::wstring currentfile(folderpath + findfiledata.cFileName);
					CollectFile(currentfile);
				}
			}
		}
		while (FindNextFileW(hFind, &findfiledata) && !cancellationflag_);

		FindClose(hFind);
	}

	return 0;
}

BOOL ImgBrowser::IsFileTypeSupported(LPCTSTR fileName)
{
	auto extension = PathFindExtension(fileName);
	return StrCmpI(extension, L".bmp") == 0
		|| StrCmpI(extension, L".gif") == 0
		|| StrCmpI(extension, L".jpg") == 0
		|| StrCmpI(extension, L".jpeg") == 0
		|| StrCmpI(extension, L".png") == 0
		|| StrCmpI(extension, L".ico") == 0
		|| StrCmpI(extension, L".tif") == 0
		|| StrCmpI(extension, L".tiff") == 0;
}

void ImgBrowser::StartBrowsingAsync(std::wstring path, INT targetwidth, INT targetheight)
{
	Reset();

	targetwidth_ = targetwidth;
	targetheight_ = targetheight;

	WIN32_FIND_DATA findfiledata;
	HANDLE findfilehandle;
	BOOL forcedfolder = FALSE;

	if (path.size() == 0)
	{
		WCHAR currentdirectory[32768];
		GetCurrentDirectory(sizeof(currentdirectory), currentdirectory);
		path = currentdirectory;
	}

	if (path.back() == L'\\')
	{
		path = path.substr(0, path.size() - 1);
		forcedfolder = TRUE;
	}

	findfilehandle = FindFirstFileW(path.c_str(), &findfiledata);
	if (findfilehandle != INVALID_HANDLE_VALUE)
	{
		FindClose(findfilehandle);
		if (!(findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !forcedfolder)
		{
			auto backslashposition = path.rfind(L"\\");
			if (backslashposition != std::wstring::npos)
			{
				folderpath_ = path.substr(0, backslashposition + 1);
			}
			else
			{
				folderpath_ = L".\\";
				path = folderpath_ + path;
			}

			CollectFile(path);
		}
		else
		{
			folderpath_ = path + L"\\";
		}
	}
	else
	{
		WCHAR mypicturespath[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYPICTURES, NULL, SHGFP_TYPE_CURRENT, mypicturespath)))
		{
			folderpath_ = std::wstring(mypicturespath) + L"\\";
		}
		else
		{
			// TODO: handle error
		}
	}

	collectorthread_ = CreateThread(NULL, 0, StaticThreadCollect, (void*)this, 0, NULL);
}

void ImgBrowser::StopCollecting()
{
	cancellationflag_ = TRUE;

	if (WaitForSingleObject(collectorthread_, INFINITE) != WAIT_OBJECT_0)
	{
		// TODO: handle error
	}

	cancellationflag_ = FALSE;
}

void ImgBrowser::StopBrowsing()
{
	StopCollecting();
	if (collectorthread_ != NULL)
	{
		CloseHandle(collectorthread_);
		collectorthread_ = NULL;
	}

	loader_.StopLoading();
}

void ImgBrowser::Reset()
{
	files_.clear();
	currentfileiterator_ = files_.end();
}

std::wstring ImgBrowser::GetCurrentFilePath()
{
	if (currentfileiterator_ != files_.end())
	{
		return *currentfileiterator_;
	}
	else
	{
		return std::wstring();
	}
}

ImgItem* ImgBrowser::GetCurrentItem()
{
	if (currentfileiterator_ != files_.end())
	{
		auto imgitem = cache_.Get(*currentfileiterator_).get();
		if (imgitem != nullptr)
		{
			if (imgitem->status() == ImgItem::Status::Queued)
			{
				loader_.LoadNextAsync(imgitem);
			}

			if (WaitForSingleObject(imgitem->loadedevent(), INFINITE) != WAIT_OBJECT_0)
			{
				// TODO: handle error
			}

			return imgitem;
		}
	}

	return nullptr;
}

BOOL ImgBrowser::MoveToNext()
{
	BOOL moveSuccess = FALSE;
	EnterCriticalSection(&navigatecriticalsection_);
	if (currentfileiterator_ != files_.end() && std::next(currentfileiterator_) != files_.end())
	{
		++currentfileiterator_;
		moveSuccess = TRUE;
	}

	LeaveCriticalSection(&navigatecriticalsection_);
	return moveSuccess;
}

BOOL ImgBrowser::MoveToPrevious()
{
	BOOL moveSuccess = FALSE;
	EnterCriticalSection(&navigatecriticalsection_);
	if (currentfileiterator_ != files_.begin())
	{
		--currentfileiterator_;
		moveSuccess = TRUE;
	}

	LeaveCriticalSection(&navigatecriticalsection_);
	return moveSuccess;
}

DWORD WINAPI ImgBrowser::StaticThreadCollect(void* browserinstance)
{
	ImgBrowser* browser = (ImgBrowser*)browserinstance;
	return browser->CollectFolder(browser->folderpath_);
}

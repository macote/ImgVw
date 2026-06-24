#include "ImgBrowser.h"
#include <Shlwapi.h>

void ImgBrowser::CollectFile(const std::wstring& filepath, ImgItem::Format imgformat)
{
    EnterCriticalSection(&browsecriticalsection_);

    if (files_.Add(filepath))
    {
        cache_.Add(filepath, targetwidth_, targetheight_, imgformat);
        loader_.QueueItem(cache_.Get(filepath));
    }

    SetEvent(readyevent_);
    NotifyChanged();

    LeaveCriticalSection(&browsecriticalsection_);
}

void ImgBrowser::CollectFolder(const std::wstring& folderpath)
{
    WIN32_FIND_DATA findfiledata;
    HANDLE hFind;
    std::wstring pattern = folderpath + L"*";
    hFind = FindFirstFile(pattern.c_str(), &findfiledata);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (lstrcmp(findfiledata.cFileName, L".") != 0 && lstrcmp(findfiledata.cFileName, L"..") != 0)
                {
                    std::wstring currentpath(folderpath + findfiledata.cFileName + L"\\");
                    if (recursive_)
                    {
                        CollectFolder(currentpath);
                    }
                    else
                    {
                        folders_.push_back(currentpath);
                    }
                }
            }
            else
            {
                std::wstring currentfile(folderpath + findfiledata.cFileName);
                const auto imgformat = ResolveFileFormat(currentfile);
                if (imgformat != ImgItem::Format::Unsupported)
                {
                    CollectFile(currentfile, imgformat);
                }
            }
        } while (FindNextFile(hFind, &findfiledata) && !cancellationflag_);

        FindClose(hFind);
    }
}

ImgItem::Format ImgBrowser::ResolveFileFormat(const std::wstring& filepath)
{
    return ImgItemFactory::ResolveFormat(filepath);
}

void ImgBrowser::BrowseAsync(const std::wstring& path, INT targetwidth, INT targetheight)
{
    Reset();

    targetwidth_ = targetwidth;
    targetheight_ = targetheight;

    WIN32_FIND_DATA findfiledata{};
    HANDLE findfilehandle{};
    BOOL forcedfolder{};
    std::wstring workpath = path;

    if (workpath.empty())
    {
        const INT BUFFERSIZE = 4096;
        TCHAR currentdirectory[BUFFERSIZE];
        GetCurrentDirectory(BUFFERSIZE, currentdirectory);
        workpath = currentdirectory;
    }

    if (workpath.back() == L'\\')
    {
        workpath = workpath.substr(0, workpath.size() - 1);
        forcedfolder = TRUE;
    }

    findfilehandle = FindFirstFile(workpath.c_str(), &findfiledata);
    if (findfilehandle != INVALID_HANDLE_VALUE)
    {
        FindClose(findfilehandle);
        if (!(findfiledata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !forcedfolder)
        {
            const auto backslashposition = workpath.rfind(L'\\');
            if (backslashposition != std::wstring::npos)
            {
                folderpath_ = workpath.substr(0, backslashposition + 1);
            }
            else
            {
                folderpath_ = L".\\";
                workpath = folderpath_ + workpath;
            }

            const auto imgformat = ResolveFileFormat(workpath);
            if (imgformat != ImgItem::Format::Unsupported)
            {
                CollectFile(workpath, imgformat);
            }
        }
        else
        {
            folderpath_ = workpath + L"\\";
        }
    }
    else
    {
        TCHAR mypicturespath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_MYPICTURES, nullptr, SHGFP_TYPE_CURRENT, mypicturespath)))
        {
            folderpath_ = std::wstring(mypicturespath) + L"\\";
        }
        else
        {
            // TODO: handle error
        }
    }

    ResetEvent(readyevent_);

    collectorthread_ = CreateThread(nullptr, 0, StaticThreadCollect, reinterpret_cast<void*>(this), 0, nullptr);
}

void ImgBrowser::BrowseSubFoldersAsync()
{
    if (recursive_)
    {
        return;
    }

    recursive_ = TRUE;

    const auto collectorstatus = WaitForSingleObject(collectorthread_, 0);
    if (collectorstatus == WAIT_TIMEOUT)
    {
        return;
    }

    if (collectorstatus != WAIT_OBJECT_0)
    {
        // TODO: handle error
        return;
    }

    if (!cancellationflag_ && !folders_.empty())
    {
        ResetEvent(readyevent_);

        CloseHandle(collectorthread_);
        collectorthread_ =
            CreateThread(nullptr, 0, StaticThreadCollectSubFolders, reinterpret_cast<void*>(this), 0, nullptr);
    }
}

void ImgBrowser::StopCollecting()
{
    if (collectorthread_ == nullptr || collectorthread_ == INVALID_HANDLE_VALUE)
    {
        return;
    }

    cancellationflag_ = TRUE;

    const DWORD timeoutMs = 3000;
    const DWORD waitResult = WaitForSingleObject(collectorthread_, timeoutMs);
    if (waitResult == WAIT_TIMEOUT)
    {
#if _DEBUG
        OutputDebugString(L"ImgBrowser::StopCollecting: Warning: collector thread did not terminate within timeout.\n");
#endif
    }
    else if (waitResult == WAIT_FAILED)
    {
#if _DEBUG
        const DWORD error = GetLastError();
        WCHAR buf[256];
        swprintf_s(buf, L"ImgBrowser::StopCollecting: WaitForSingleObject failed with error 0x%08lX\n",
                   static_cast<unsigned long>(error));
        OutputDebugString(buf);
#endif
    }

    cancellationflag_ = FALSE;
}

void ImgBrowser::StopBrowsing()
{
    StopCollecting();
    if (collectorthread_ != nullptr)
    {
        CloseHandle(collectorthread_);
        collectorthread_ = nullptr;
    }

    loader_.StopLoading();
}

void ImgBrowser::SetNotificationWindow(HWND hwnd, UINT message)
{
    notificationhwnd_ = hwnd;
    notificationmessage_ = message;
    loader_.SetNotificationWindow(hwnd, message);
}

void ImgBrowser::Reset()
{
    files_.Clear();
}

std::wstring ImgBrowser::GetCurrentFilePath()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    LeaveCriticalSection(&browsecriticalsection_);
    return filepath;
}

std::shared_ptr<ImgItem> ImgBrowser::GetCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    const auto imgitem = filepath.empty() ? std::shared_ptr<ImgItem>() : cache_.Get(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    if (imgitem != nullptr)
    {
        if (imgitem->status() == ImgItem::Status::Queued)
        {
            loader_.QueueItem(imgitem, TRUE);
        }

        return imgitem;
    }

    return {};
}

void ImgBrowser::ReloadCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    const auto imgitem = filepath.empty() ? std::shared_ptr<ImgItem>() : cache_.Get(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    if (imgitem != nullptr)
    {
        if (imgitem->status() != ImgItem::Status::Queued)
        {
            imgitem->Unload();
        }

        loader_.QueueItem(imgitem, TRUE);
    }
}

BOOL ImgBrowser::MoveToNext()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToNext();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToPrevious()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToPrevious();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToFirst()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToFirst();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToLast()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToLast();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToItem(const std::wstring& filepath)
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveTo(filepath);
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToRandom()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto moveSuccess = files_.MoveToRandom();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

void ImgBrowser::RemoveCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    const auto filepath = files_.CurrentPath();
    if (!filepath.empty())
    {
        cache_.Remove(filepath);
        files_.RemoveCurrent();
    }

    LeaveCriticalSection(&browsecriticalsection_);
}

void ImgBrowser::CollectSubFolders()
{
    for (const auto& folder : folders_)
    {
        if (cancellationflag_)
        {
            break;
        }
        CollectFolder(folder);
    }

    folders_.clear();
}

void ImgBrowser::NotifyChanged()
{
    if (notificationhwnd_ != nullptr && notificationmessage_ != 0)
    {
        PostMessage(notificationhwnd_, notificationmessage_, 0, 0);
    }
}

DWORD WINAPI ImgBrowser::StaticThreadCollect(void* browserinstance)
{
    ImgBrowser* browser = reinterpret_cast<ImgBrowser*>(browserinstance);

    browser->CollectFolder(browser->folderpath_);
    if (browser->recursive_)
    {
        browser->CollectSubFolders();
    }

    SetEvent(browser->readyevent_);
    browser->NotifyChanged();

    return 0;
}

DWORD WINAPI ImgBrowser::StaticThreadCollectSubFolders(void* browserinstance)
{
    ImgBrowser* browser = reinterpret_cast<ImgBrowser*>(browserinstance);

    browser->CollectSubFolders();

    SetEvent(browser->readyevent_);
    browser->NotifyChanged();

    return 0;
}

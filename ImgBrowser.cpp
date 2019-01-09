#include "ImgBrowser.h"
#include <Shlwapi.h>

void ImgBrowser::CollectFile(const std::wstring& filepath)
{
    EnterCriticalSection(&browsecriticalsection_);

    files_.insert(filepath);
    cache_.Add(filepath, targetwidth_, targetheight_);
    randomlist_.push_back(filepath);
    loader_.QueueItem(cache_.Get(filepath).get());

    if (currentfileiterator_ == files_.end())
    {
        currentfileiterator_ = files_.begin();
    }

    SetEvent(readyevent_);

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
                if (IsFileFormatSupported(findfiledata.cFileName))
                {
                    std::wstring currentfile(folderpath + findfiledata.cFileName);
                    CollectFile(currentfile);
                }
            }
        }
        while (FindNextFile(hFind, &findfiledata) && !cancellationflag_);

        FindClose(hFind);
    }
}

BOOL ImgBrowser::IsFileFormatSupported(LPCTSTR filename)
{
    return ImgItemHelper::GetImgFormatFromExtension(filename) != ImgItem::Format::Unsupported;
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

    if (workpath.size() == 0)
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
            const auto backslashposition = workpath.rfind(L"\\");
            if (backslashposition != std::wstring::npos)
            {
                folderpath_ = workpath.substr(0, backslashposition + 1);
            }
            else
            {
                folderpath_ = L".\\";
                workpath = folderpath_ + workpath;
            }

            CollectFile(workpath);
        }
        else
        {
            folderpath_ = workpath + L"\\";
        }
    }
    else
    {
        TCHAR mypicturespath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYPICTURES, NULL, SHGFP_TYPE_CURRENT, mypicturespath)))
        {
            folderpath_ = std::wstring(mypicturespath) + L"\\";
        }
        else
        {
            // TODO: handle error
        }
    }

    ResetEvent(readyevent_);

    collectorthread_ = CreateThread(NULL, 0, StaticThreadCollect, reinterpret_cast<void*>(this), 0, NULL);
}

void ImgBrowser::BrowseSubFoldersAsync()
{
    if (recursive_)
    {
        return;
    }

    recursive_ = TRUE;

    if (WaitForSingleObject(collectorthread_, INFINITE) != WAIT_OBJECT_0)
    {
        // TODO: handle error
    }

    if (!cancellationflag_ && folders_.size() > 0)
    {
        ResetEvent(readyevent_);

        CloseHandle(collectorthread_);
        collectorthread_ = CreateThread(NULL, 0, StaticThreadCollectSubFolders, reinterpret_cast<void*>(this), 0, NULL);
    }
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
    randomlist_.clear();
    currentrandomindex_ = 0x80000000;
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

const ImgItem* ImgBrowser::GetCurrentItem()
{
    if (currentfileiterator_ != files_.end())
    {
        const auto imgitem = cache_.Get(*currentfileiterator_).get();
        if (imgitem != nullptr)
        {
            if (imgitem->status() == ImgItem::Status::Queued)
            {
                loader_.QueueItem(imgitem, TRUE);
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

void ImgBrowser::ReloadCurrentItem()
{
    if (currentfileiterator_ != files_.end())
    {
        auto imgitem = cache_.Get(*currentfileiterator_).get();
        if (imgitem != nullptr)
        {
            if (imgitem->status() != ImgItem::Status::Queued)
            {
                imgitem->Unload();
            }

            loader_.QueueItem(imgitem, TRUE);

            if (WaitForSingleObject(imgitem->loadedevent(), INFINITE) != WAIT_OBJECT_0)
            {
                // TODO: handle error
            }
        }
    }
}

BOOL ImgBrowser::MoveToNext()
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
    if (files_.size() > 0 && std::next(currentfileiterator_) != files_.end())
    {
        ++currentfileiterator_;
        moveSuccess = TRUE;
    }

    LeaveCriticalSection(&browsecriticalsection_);

    return moveSuccess;
}

BOOL ImgBrowser::MoveToPrevious()
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
    if (files_.size() > 0 && currentfileiterator_ != files_.begin())
    {
        --currentfileiterator_;
        moveSuccess = TRUE;
    }

    LeaveCriticalSection(&browsecriticalsection_);

    return moveSuccess;
}

BOOL ImgBrowser::MoveToFirst()
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
    if (files_.size() > 0 && currentfileiterator_ != files_.begin())
    {
        currentfileiterator_ = files_.begin();
        moveSuccess = TRUE;
    }

    LeaveCriticalSection(&browsecriticalsection_);

    return moveSuccess;
}

BOOL ImgBrowser::MoveToLast()
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
    if (files_.size() > 0 && std::next(currentfileiterator_) != files_.end())
    {
        currentfileiterator_ = std::prev(files_.end());
        moveSuccess = TRUE;
    }

    LeaveCriticalSection(&browsecriticalsection_);

    return moveSuccess;
}

BOOL ImgBrowser::MoveToItem(std::wstring filepath)
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
    if (files_.size() > 0)
    {
        currentfileiterator_ = files_.find(filepath);
        moveSuccess = currentfileiterator_ != files_.end();
    }

    LeaveCriticalSection(&browsecriticalsection_);

    return moveSuccess;
}

BOOL ImgBrowser::MoveToRandom()
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
    if (files_.size() > 0)
    {
        if (currentrandomindex_ >= randomlist_.size())
        {
            std::wstring last;
            if (currentrandomindex_ != kIndexPark)
            {
                last = randomlist_[currentrandomindex_ - 1];
            }

            std::shuffle(std::begin(randomlist_), std::end(randomlist_), rnge_);
            if (last == *randomlist_.begin() && randomlist_.size() > 1)
            {
                do
                {
                    std::shuffle(std::begin(randomlist_), std::end(randomlist_), rnge_);
                }
                while (last == *randomlist_.begin());
            }

            currentrandomindex_ = 0;
        }

        auto filepath = randomlist_[currentrandomindex_];
        ++currentrandomindex_;
        moveSuccess = MoveToItem(filepath);
    }

    LeaveCriticalSection(&browsecriticalsection_);

    return moveSuccess;
}

void ImgBrowser::RemoveCurrentItem()
{
    EnterCriticalSection(&browsecriticalsection_);
    if (currentfileiterator_ != files_.end())
    {
        cache_.Remove(*currentfileiterator_);
        files_.erase(currentfileiterator_++);
    }

    LeaveCriticalSection(&browsecriticalsection_);
}

void ImgBrowser::CollectSubFolders()
{
    for (const auto & folder : folders_)
    {
        CollectFolder(folder);
    }

    folders_.clear();
}

void ImgBrowser::GetReady()
{
    if (WaitForSingleObject(readyevent_, INFINITE) != WAIT_OBJECT_0)
    {
        // TODO: handle error
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

    return 0;
}

DWORD WINAPI ImgBrowser::StaticThreadCollectSubFolders(void* browserinstance)
{
    ImgBrowser* browser = reinterpret_cast<ImgBrowser*>(browserinstance);

    browser->CollectSubFolders();

    SetEvent(browser->readyevent_);

    return 0;
}
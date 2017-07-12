#include "ImgBrowser.h"
#include <shlobj.h>
#include <Shlwapi.h>
#include <string>
#include <iomanip>
#include <sstream>

void ImgBrowser::CollectFile(std::wstring filepath)
{
    EnterCriticalSection(&browsecriticalsection_);
    files_.insert(filepath);
    randomlist_.push_back(filepath);
    cache_.Add(filepath, targetwidth_, targetheight_);
    loader_.LoadAsync(cache_.Get(filepath).get());
    if (currentfileiterator_ == files_.end())
    {
        currentfileiterator_ = files_.begin();
    }

    LeaveCriticalSection(&browsecriticalsection_);
}

DWORD ImgBrowser::CollectFolder(std::wstring folderpath)
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
                    // TODO: add support for recursivity or some sort of folder navigation
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

    return 0;
}

BOOL ImgBrowser::IsFileFormatSupported(LPCTSTR filename)
{
    return ImgItemHelper::GetImgFormatFromExtension(filename) != ImgItem::Format::Unsupported;
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

    findfilehandle = FindFirstFile(path.c_str(), &findfiledata);
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
    EnterCriticalSection(&browsecriticalsection_);
    if (std::next(currentfileiterator_) != files_.end())
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
    if (currentfileiterator_ != files_.begin())
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
    if (currentfileiterator_ != files_.begin())
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
    if (std::next(currentfileiterator_) != files_.end())
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
    currentfileiterator_ = files_.find(filepath);
    moveSuccess = currentfileiterator_ != files_.end();
    LeaveCriticalSection(&browsecriticalsection_);
    return moveSuccess;
}

BOOL ImgBrowser::MoveToRandom()
{
    BOOL moveSuccess = FALSE;
    EnterCriticalSection(&browsecriticalsection_);
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

DWORD WINAPI ImgBrowser::StaticThreadCollect(void* browserinstance)
{
    ImgBrowser* browser = (ImgBrowser*)browserinstance;
    return browser->CollectFolder(browser->folderpath_);
}

void ImgBrowser::InitializeTempPath()
{
    TCHAR temppathbuffer[MAX_PATH];

    auto pathlen = GetTempPath(MAX_PATH, temppathbuffer);
    if (pathlen > MAX_PATH || pathlen == 0)
    {
        // TODO: handle error.
    }

    std::wstring temppath = temppathbuffer;

    if (temppathbuffer[pathlen - 1] != L'\\')
    {
        temppath += L'\\';
    }

    std::wstring imgtemppath = temppath;
    std::wstring testpath;
    WIN32_FIND_DATA finddata;
    HANDLE find;
    BOOL folderexists = TRUE;

    do
    {
        GUID guid;
        if (CoCreateGuid(&guid) != S_OK)
        {
            // TODO: handle error.
        }

        std::wstringstream wss;
        wss << std::setw(8) << std::setfill(L'0') << std::uppercase << std::hex << guid.Data1;
        std::wstring testpath = temppath + wss.str();

        find = FindFirstFile(testpath.c_str(), &finddata);
        if (find == INVALID_HANDLE_VALUE)
        {
            CreateDirectory(testpath.c_str(), NULL);
            folderexists = FALSE;
            temppath_ = testpath;
        }
        else
        {
            FindClose(find);
        }
    } 
    while (folderexists);
}

void ImgBrowser::DeleteTempPath()
{
    RemoveDirectory(temppath_.c_str());
}
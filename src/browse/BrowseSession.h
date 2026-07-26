#pragma once

#include "ImgFileList.h"
#include "ImgLoader.h"
#include "Win32Handle.h"

#include <Windows.h>

#include <string>
#include <memory>
#include <vector>

class BrowseSessionCancellation final
{
  public:
    BrowseSessionCancellation();

    bool valid() const;
    DWORD error() const;
    HANDLE event() const;
    bool Cancelled() const;
    bool Signal();

  private:
    Win32Handle event_;
    DWORD error_{ERROR_SUCCESS};
};

struct BrowseSessionCollection
{
    std::shared_ptr<BrowseSessionCancellation> cancellation;
    std::wstring folderpath;
    volatile LONG recursive{};
    BOOL subfolders_only{};
    ULONG generation{};
};

class BrowseSession final
{
  public:
    BrowseSession();

    bool ReadyEventValid() const;
    DWORD ready_event_error() const;
    bool ResetReadyEvent();
    bool SignalReadyEvent();
    void SetCollectorThread(HANDLE thread, std::shared_ptr<BrowseSessionCancellation> cancellation);
    std::shared_ptr<BrowseSessionCollection> BeginCollection(ULONG generation);
    std::shared_ptr<BrowseSessionCollection> collection() const;
    bool HasCollectorThread() const;
    HANDLE collector_thread() const;
    std::shared_ptr<BrowseSessionCancellation> cancellation() const;
    void ClearCollectorThread();
    void Reset(ULONG generation);
    ULONG generation() const;
    DWORD collection_error() const;
    void SetCollectionError(DWORD error);
    void SetCollectionErrorIfClear(DWORD error);

    std::wstring& folderpath();
    const std::wstring& folderpath() const;
    void set_folderpath(std::wstring folderpath);

    ImgFileList& files();
    const ImgFileList& files() const;

    std::vector<std::wstring>& folders();
    const std::vector<std::wstring>& folders() const;

  private:
    std::wstring folderpath_;
    ImgFileList files_;
    std::vector<std::wstring> folders_;
    volatile LONG generation_{static_cast<LONG>(NextImgGeneration())};
    DWORD collection_error_{ERROR_SUCCESS};
    Win32Handle ready_event_;
    DWORD ready_event_error_{ERROR_SUCCESS};
    Win32Handle collector_thread_;
    std::shared_ptr<BrowseSessionCancellation> cancellation_;
    std::weak_ptr<BrowseSessionCollection> collection_;
};

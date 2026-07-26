#include "BrowseSession.h"

#include <utility>

BrowseSessionCancellation::BrowseSessionCancellation()
{
    event_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    error_ = event_.valid() ? ERROR_SUCCESS : GetLastError();
}

bool BrowseSessionCancellation::valid() const
{
    return event_.valid();
}

DWORD BrowseSessionCancellation::error() const
{
    return error_;
}

HANDLE BrowseSessionCancellation::event() const
{
    return event_.get();
}

bool BrowseSessionCancellation::Cancelled() const
{
    return event_.valid() && WaitForSingleObject(event_.get(), 0) == WAIT_OBJECT_0;
}

bool BrowseSessionCancellation::Signal()
{
    return event_.valid() && SetEvent(event_.get()) != FALSE;
}

BrowseSession::BrowseSession()
{
    ready_event_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    ready_event_error_ = ready_event_.valid() ? ERROR_SUCCESS : GetLastError();
}

bool BrowseSession::ReadyEventValid() const
{
    return ready_event_.valid();
}

DWORD BrowseSession::ready_event_error() const
{
    return ready_event_error_;
}

bool BrowseSession::ResetReadyEvent()
{
    return ready_event_.valid() && ResetEvent(ready_event_.get()) != FALSE;
}

bool BrowseSession::SignalReadyEvent()
{
    return ready_event_.valid() && SetEvent(ready_event_.get()) != FALSE;
}

void BrowseSession::SetCollectorThread(HANDLE thread, std::shared_ptr<BrowseSessionCancellation> cancellation)
{
    collector_thread_.reset(thread);
    cancellation_ = std::move(cancellation);
}

std::shared_ptr<BrowseSessionCollection> BrowseSession::BeginCollection(ULONG generation)
{
    auto collection = std::make_shared<BrowseSessionCollection>();
    collection->cancellation = std::make_shared<BrowseSessionCancellation>();
    collection->generation = generation;
    collection_ = collection;
    return collection;
}

std::shared_ptr<BrowseSessionCollection> BrowseSession::collection() const
{
    return collection_.lock();
}

bool BrowseSession::HasCollectorThread() const
{
    return collector_thread_.valid();
}

HANDLE BrowseSession::collector_thread() const
{
    return collector_thread_.get();
}

std::shared_ptr<BrowseSessionCancellation> BrowseSession::cancellation() const
{
    return cancellation_;
}

void BrowseSession::ClearCollectorThread()
{
    collector_thread_.reset();
    cancellation_.reset();
    collection_.reset();
}

void BrowseSession::Reset(ULONG generation)
{
    folderpath_.clear();
    files_.Clear();
    folders_.clear();
    InterlockedExchange(&generation_, static_cast<LONG>(generation));
    collection_error_ = ERROR_SUCCESS;
}

ULONG BrowseSession::generation() const
{
    return static_cast<ULONG>(InterlockedCompareExchange(const_cast<volatile LONG*>(&generation_), 0, 0));
}

DWORD BrowseSession::collection_error() const
{
    return collection_error_;
}

void BrowseSession::SetCollectionError(DWORD error)
{
    collection_error_ = error;
}

void BrowseSession::SetCollectionErrorIfClear(DWORD error)
{
    if (collection_error_ == ERROR_SUCCESS)
    {
        collection_error_ = error;
    }
}

const std::wstring& BrowseSession::folderpath() const
{
    return folderpath_;
}

std::wstring& BrowseSession::folderpath()
{
    return folderpath_;
}

void BrowseSession::set_folderpath(std::wstring folderpath)
{
    folderpath_ = std::move(folderpath);
}

ImgFileList& BrowseSession::files()
{
    return files_;
}

const ImgFileList& BrowseSession::files() const
{
    return files_;
}

std::vector<std::wstring>& BrowseSession::folders()
{
    return folders_;
}

const std::vector<std::wstring>& BrowseSession::folders() const
{
    return folders_;
}

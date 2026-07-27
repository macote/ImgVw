#pragma once

#include "Win32Handle.h"
#include <Windows.h>
#include <utility>

enum class CountingSemaphoreWaitStatus
{
    Acquired,
    Cancelled,
    Failed
};

class CountingSemaphore final
{
  public:
    CountingSemaphore() noexcept {}
    CountingSemaphore(LONG maximumcount) noexcept
    {
        SetupSemaphore(maximumcount);
    }
    ~CountingSemaphore()
    {
        Close();
    }
    CountingSemaphore(const CountingSemaphore&) = delete;
    CountingSemaphore(CountingSemaphore&& other)
    {
        *this = std::move(other);
    }
    CountingSemaphore& operator=(CountingSemaphore&& other)
    {
        if (this != &other)
        {
            Close();

            semaphore_ = std::move(other.semaphore_);
        }

        return *this;
    }
    bool Notify() const;
    CountingSemaphoreWaitStatus Wait(HANDLE cancellation_event = nullptr) const;
    bool SetupSemaphore(LONG maximumcount);
    bool valid() const
    {
        return semaphore_.valid();
    }

  private:
    Win32Handle semaphore_;

  private:
    void Close()
    {
        semaphore_.reset();
    }
};

inline bool CountingSemaphore::SetupSemaphore(LONG maximumcount)
{
    Close();

    semaphore_.reset(CreateSemaphore(NULL, maximumcount, maximumcount, NULL));
    if (!semaphore_.valid())
    {
        return false;
    }

    return true;
}

inline bool CountingSemaphore::Notify() const
{
    return ReleaseSemaphore(semaphore_.get(), 1, NULL) != FALSE;
}

inline CountingSemaphoreWaitStatus CountingSemaphore::Wait(HANDLE cancellation_event) const
{
    if (cancellation_event == nullptr || cancellation_event == INVALID_HANDLE_VALUE)
    {
        return WaitForSingleObject(semaphore_.get(), INFINITE) == WAIT_OBJECT_0 ? CountingSemaphoreWaitStatus::Acquired
                                                                                : CountingSemaphoreWaitStatus::Failed;
    }

    const HANDLE handles[] = {cancellation_event, semaphore_.get()};
    const auto result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (result == WAIT_OBJECT_0)
    {
        return CountingSemaphoreWaitStatus::Cancelled;
    }
    if (result == WAIT_OBJECT_0 + 1)
    {
        return CountingSemaphoreWaitStatus::Acquired;
    }

    return CountingSemaphoreWaitStatus::Failed;
}

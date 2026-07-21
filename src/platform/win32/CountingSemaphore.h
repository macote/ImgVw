#pragma once

#include "Win32Handle.h"
#include <Windows.h>
#include <utility>

class CountingSemaphore final
{
  public:
    CountingSemaphore() {}
    CountingSemaphore(LONG maximumcount)
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
    void Notify() const;
    void Wait() const;
    void SetupSemaphore(LONG maximumcount);

  private:
    Win32Handle semaphore_;

  private:
    void Close()
    {
        semaphore_.reset();
    }
};

inline void CountingSemaphore::SetupSemaphore(LONG maximumcount)
{
    Close();

    semaphore_.reset(CreateSemaphore(NULL, maximumcount, maximumcount, NULL));
    if (!semaphore_.valid())
    {
        // TODO: handle error
    }
}

inline void CountingSemaphore::Notify() const
{
    if (!ReleaseSemaphore(semaphore_.get(), 1, NULL))
    {
        // TODO: handle error
    }
}

inline void CountingSemaphore::Wait() const
{
    switch (WaitForSingleObject(semaphore_.get(), INFINITE))
    {
    case WAIT_OBJECT_0:
        break;
    case WAIT_FAILED:
        // TODO: handle unhandled case
        break;
    }
}

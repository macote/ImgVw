#pragma once

#include <Windows.h>
#include <utility>

class CountingSemaphore final
{
public:
    CountingSemaphore() { }
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

            semaphore_ = other.semaphore_;
            other.semaphore_ = INVALID_HANDLE_VALUE;
        }

        return *this;
    }
    void Notify() const;
    void Wait() const;
    void SetupSemaphore(LONG maximumcount);
private:
    void Close()
    {
        if (semaphore_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(semaphore_);
            semaphore_ = INVALID_HANDLE_VALUE;
        }
    }
    HANDLE semaphore_{ INVALID_HANDLE_VALUE };
};

inline void CountingSemaphore::SetupSemaphore(LONG maximumcount)
{
    Close();

    semaphore_ = CreateSemaphore(NULL, maximumcount, maximumcount, NULL);
    if (semaphore_ == NULL)
    {
        // TODO: handle error
    }
}

inline void CountingSemaphore::Notify() const
{
    if (!ReleaseSemaphore(semaphore_, 1, NULL))
    {
        // TODO: handle error
    }
}

inline void CountingSemaphore::Wait() const
{
    switch (WaitForSingleObject(semaphore_, INFINITE))
    {
    case WAIT_OBJECT_0:
        break;
    case WAIT_FAILED:
        // TODO: handle unhandled case
        break;
    }
}
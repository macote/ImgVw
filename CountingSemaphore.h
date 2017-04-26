#pragma once

#include <Windows.h>
#include <utility>

class CountingSemaphore
{
public:
	CountingSemaphore()
	{
	}
	CountingSemaphore(LONG maximumcount) : maximumcount_(maximumcount)
	{
		if (maximumcount > 0)
		{
			SetupSemaphore();
		}
	}
	~CountingSemaphore()
	{
		if (semaphore_ != INVALID_HANDLE_VALUE)
		{
			CloseHandle(semaphore_);
		}
	}
	CountingSemaphore(CountingSemaphore&& other)
	{
		*this = std::move(other);
	}
	CountingSemaphore& operator=(CountingSemaphore&& other)
	{
		if (this != &other)
		{
			semaphore_ = other.semaphore_;
			other.semaphore_ = INVALID_HANDLE_VALUE;
		}

		return *this;
	}
	void Notify();
	void Wait();
	LONG maximumcount() const { return maximumcount_; }
	void set_maximumcount(LONG maximumcount);
private:
	void SetupSemaphore();
	LONG maximumcount_{};
	HANDLE semaphore_{ INVALID_HANDLE_VALUE };
};

inline void CountingSemaphore::set_maximumcount(LONG maximumcount)
{
	maximumcount_ = maximumcount;
	SetupSemaphore();
}

inline void CountingSemaphore::SetupSemaphore()
{
	semaphore_ = CreateSemaphore(NULL, maximumcount_, maximumcount_, NULL);
	if (semaphore_ == NULL)
	{
		// TODO: handle error
	}
}

inline void CountingSemaphore::Notify()
{
	if (!ReleaseSemaphore(semaphore_, 1, NULL))
	{
		// TODO: handle error
	}
}

inline void CountingSemaphore::Wait()
{
	auto waitresult = WaitForSingleObject(semaphore_, INFINITE);
	switch (waitresult)
	{
	case WAIT_OBJECT_0:
		break;
	case WAIT_TIMEOUT:
		// TODO: handle unhandled case
		break;
	}
}
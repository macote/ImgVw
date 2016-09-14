#pragma once

#include <Windows.h>

class CountSemaphore
{
public:
	explicit CountSemaphore(LONG maximumcount = 0);
	CountSemaphore(const CountSemaphore&) = delete;
	CountSemaphore& operator=(const CountSemaphore&) = delete;
	~CountSemaphore()
	{
		if (semaphore_ != INVALID_HANDLE_VALUE)
		{
			CloseHandle(semaphore_);
		}
	}
	void Notify();
	void Wait();
	LONG maximumcount() const { return maximumcount_; }
	void set_maximumcount(LONG maximumcount);
private:
	void SetupSemaphore();
	LONG maximumcount_;
	HANDLE semaphore_{ INVALID_HANDLE_VALUE };
};

inline CountSemaphore::CountSemaphore(LONG maximumcount) : maximumcount_{ maximumcount }
{
	if (maximumcount_ > 0)
	{
		SetupSemaphore();
	}
}

inline void CountSemaphore::set_maximumcount(LONG maximumcount)
{
	maximumcount_ = maximumcount;
	SetupSemaphore();
}

inline void CountSemaphore::SetupSemaphore()
{
	semaphore_ = CreateSemaphore(NULL, maximumcount_, maximumcount_, NULL);
	if (semaphore_ == NULL)
	{
		// TODO: handle error
	}
}

inline void CountSemaphore::Notify()
{
	if (!ReleaseSemaphore(semaphore_, 1, NULL))
	{
		// TODO: handle error
	}
}

inline void CountSemaphore::Wait()
{
	auto waitresult = WaitForSingleObject(semaphore_, INFINITE);
	switch (waitresult)
	{
	case WAIT_OBJECT_0:
		break;
	case WAIT_TIMEOUT:
		// TODO: handle unhandle case
		break;
	}
}

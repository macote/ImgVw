#pragma once

#include <Windows.h>

class CriticalSection final
{
  public:
    explicit CriticalSection(DWORD spin_count = 0x00000400) noexcept
        : initialized_(InitializeCriticalSectionAndSpinCount(&critical_section_, spin_count) != FALSE)
    {
    }
    ~CriticalSection()
    {
        if (initialized_)
        {
            DeleteCriticalSection(&critical_section_);
        }
    }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    bool valid() const
    {
        return initialized_;
    }
    CRITICAL_SECTION* get()
    {
        return &critical_section_;
    }

  private:
    CRITICAL_SECTION critical_section_{};
    bool initialized_{};
};

class CriticalSectionLock final
{
  public:
    explicit CriticalSectionLock(CriticalSection& critical_section) : critical_section_(critical_section.get())
    {
        EnterCriticalSection(critical_section_);
    }
    ~CriticalSectionLock()
    {
        LeaveCriticalSection(critical_section_);
    }
    CriticalSectionLock(const CriticalSectionLock&) = delete;
    CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;

  private:
    CRITICAL_SECTION* critical_section_;
};

#pragma once

#include <Windows.h>
#include <lcms2.h>

class ColorProfile
{
  public:
    ColorProfile() = default;
    ~ColorProfile();
    ColorProfile(const ColorProfile&) = delete;
    ColorProfile& operator=(const ColorProfile&) = delete;
    ColorProfile(ColorProfile&& other) noexcept;
    ColorProfile& operator=(ColorProfile&& other) noexcept;

    static ColorProfile OpenFromMemory(const void* data, cmsUInt32Number byte_count);
    static ColorProfile CreateSrgb();

    bool IsValid() const
    {
        return profile_ != nullptr;
    }

    bool IsCmyk() const;
    cmsHPROFILE get() const
    {
        return profile_;
    }

    void Reset();

  private:
    explicit ColorProfile(cmsHPROFILE profile) : profile_(profile) {}

    cmsHPROFILE profile_{nullptr};
};

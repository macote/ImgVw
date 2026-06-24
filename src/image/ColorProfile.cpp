#include "ColorProfile.h"

ColorProfile::~ColorProfile()
{
    Reset();
}

ColorProfile::ColorProfile(ColorProfile&& other) noexcept : profile_(other.profile_)
{
    other.profile_ = nullptr;
}

ColorProfile& ColorProfile::operator=(ColorProfile&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        profile_ = other.profile_;
        other.profile_ = nullptr;
    }

    return *this;
}

ColorProfile ColorProfile::OpenFromMemory(const void* data, cmsUInt32Number byte_count)
{
    if (data == nullptr || byte_count == 0)
    {
        return {};
    }

    return ColorProfile(cmsOpenProfileFromMem(data, byte_count));
}

ColorProfile ColorProfile::CreateSrgb()
{
    return ColorProfile(cmsCreate_sRGBProfile());
}

bool ColorProfile::IsCmyk() const
{
    return profile_ != nullptr && cmsGetColorSpace(profile_) == cmsSigCmykData;
}

void ColorProfile::Reset()
{
    if (profile_ != nullptr)
    {
        cmsCloseProfile(profile_);
        profile_ = nullptr;
    }
}

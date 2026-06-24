#pragma once

#include "ColorProfile.h"

#include <Windows.h>

enum class ColorTransformStatus
{
    Succeeded,
    InvalidInput,
    MissingSourceProfile,
    CreateSrgbProfileFailed,
    CreateTransformFailed,
    AllocationFailed
};

struct ColorTransformResult
{
    ColorTransformStatus status{ColorTransformStatus::InvalidInput};

    bool Succeeded() const
    {
        return status == ColorTransformStatus::Succeeded;
    }
};

class ColorTransform
{
  public:
    static ColorTransformResult TransformCmyk8ReversedToBgr8(const ColorProfile& source_profile, INT width, INT height,
                                                             INT stride, INT new_stride, PBYTE* buffer, HANDLE heap);
};

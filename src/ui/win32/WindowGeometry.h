#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace WindowGeometry
{
constexpr UINT kDefaultDpi = 96;

inline INT ScaleForDpi(INT value, UINT dpi)
{
    return MulDiv(value, static_cast<INT>(dpi == 0 ? kDefaultDpi : dpi), static_cast<INT>(kDefaultDpi));
}

inline bool ContainsRect(const RECT& outer, const RECT& inner)
{
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}
} // namespace WindowGeometry

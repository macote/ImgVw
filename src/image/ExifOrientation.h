#pragma once

#include <cstddef>

namespace exif
{
unsigned int GetOrientation(const unsigned char* data, std::size_t size);
}

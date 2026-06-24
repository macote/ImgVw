#pragma once

#include <Windows.h>
#include <string>
#include <vector>

enum class ImageHeaderProbeStatus
{
    Succeeded,
    OpenFailed,
    ReadFailed
};

struct ImageHeaderProbeResult
{
    ImageHeaderProbeStatus status{ImageHeaderProbeStatus::OpenFailed};
    DWORD win32_error{ERROR_SUCCESS};
    std::vector<BYTE> bytes;

    bool Succeeded() const
    {
        return status == ImageHeaderProbeStatus::Succeeded;
    }
};

class ImageHeaderProbe
{
  public:
    static constexpr DWORD kDefaultPrefixByteCount = 4096;

    static ImageHeaderProbeResult ReadPrefix(const std::wstring& filepath,
                                             DWORD max_byte_count = kDefaultPrefixByteCount);
};

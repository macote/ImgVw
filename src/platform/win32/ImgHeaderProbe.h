#pragma once

#include <Windows.h>
#include <string>
#include <vector>

enum class ImgHeaderProbeStatus
{
    Succeeded,
    OpenFailed,
    ReadFailed
};

struct ImgHeaderProbeResult
{
    ImgHeaderProbeStatus status{ImgHeaderProbeStatus::OpenFailed};
    DWORD win32_error{ERROR_SUCCESS};
    std::vector<BYTE> bytes;

    bool Succeeded() const
    {
        return status == ImgHeaderProbeStatus::Succeeded;
    }
};

class ImgHeaderProbe
{
  public:
    static constexpr DWORD kDefaultPrefixByteCount = 4096;

    static ImgHeaderProbeResult ReadPrefix(const std::wstring& filepath,
                                           DWORD max_byte_count = kDefaultPrefixByteCount);
};

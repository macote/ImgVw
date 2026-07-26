#include "ImgHeaderProbe.h"
#include "Win32Handle.h"

#include <utility>

ImgHeaderProbeResult ImgHeaderProbe::ReadPrefix(const std::wstring& filepath, DWORD max_byte_count)
{
    Win32Handle file(CreateFile(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid())
    {
        return {ImgHeaderProbeStatus::OpenFailed, GetLastError(), {}};
    }

    std::vector<BYTE> bytes(max_byte_count);
    DWORD bytes_read{};
    if (!ReadFile(file.get(), bytes.data(), max_byte_count, &bytes_read, nullptr))
    {
        const DWORD error = GetLastError();
        return {ImgHeaderProbeStatus::ReadFailed, error, {}};
    }

    bytes.resize(bytes_read);
    return {ImgHeaderProbeStatus::Succeeded, ERROR_SUCCESS, std::move(bytes)};
}

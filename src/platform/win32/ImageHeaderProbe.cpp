#include "ImageHeaderProbe.h"
#include "Win32Handle.h"

#include <utility>

ImageHeaderProbeResult ImageHeaderProbe::ReadPrefix(const std::wstring& filepath, DWORD max_byte_count)
{
    Win32Handle file(CreateFile(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid())
    {
        return {ImageHeaderProbeStatus::OpenFailed, GetLastError(), {}};
    }

    std::vector<BYTE> bytes(max_byte_count);
    DWORD bytes_read{};
    if (!ReadFile(file.get(), bytes.data(), max_byte_count, &bytes_read, nullptr))
    {
        const DWORD error = GetLastError();
        return {ImageHeaderProbeStatus::ReadFailed, error, {}};
    }

    bytes.resize(bytes_read);
    return {ImageHeaderProbeStatus::Succeeded, ERROR_SUCCESS, std::move(bytes)};
}

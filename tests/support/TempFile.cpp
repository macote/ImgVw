#include "TempFile.h"

#include "TestHarness.h"
#include "Win32Handle.h"

#include <Windows.h>

std::wstring TempPath(const wchar_t* filename)
{
    wchar_t path[MAX_PATH]{};
    const auto length = GetTempPathW(MAX_PATH, path);
    Check(length > 0 && length < MAX_PATH, "temporary path is available");
    return std::wstring(path) + L"ImgVwTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + filename;
}

void WriteBytes(const std::wstring& path, const std::vector<unsigned char>& bytes)
{
    Win32Handle file(
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(file.valid(), "test file can be created");
    if (!file.valid())
    {
        return;
    }

    DWORD bytes_written{};
    Check(WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &bytes_written, nullptr) &&
              bytes_written == bytes.size(),
          "test file bytes are written");
}

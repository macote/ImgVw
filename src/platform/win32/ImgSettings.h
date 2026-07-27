#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <iomanip>
#include <sstream>
#include <string>

enum class ImgSettingsStatus
{
    Succeeded,
    TempPathFailed,
    GuidCreationFailed,
    TempDirectoryCreationFailed
};

struct ImgSettingsInitializationResult
{
    ImgSettingsStatus status{ImgSettingsStatus::Succeeded};
    HRESULT system_error{S_OK};
    std::wstring temp_path;

    bool Succeeded() const
    {
        return status == ImgSettingsStatus::Succeeded;
    }
};

struct ImgSettingsOperations
{
    using GetTempPathFunction = DWORD(WINAPI*)(DWORD, LPWSTR);
    using CreateGuidFunction = HRESULT(WINAPI*)(GUID*);
    using CreateDirectoryFunction = BOOL(WINAPI*)(LPCWSTR, LPSECURITY_ATTRIBUTES);
    using GetLastErrorFunction = DWORD(WINAPI*)();

    GetTempPathFunction get_temp_path{GetTempPathW};
    CreateGuidFunction create_guid{CoCreateGuid};
    CreateDirectoryFunction create_directory{CreateDirectoryW};
    GetLastErrorFunction get_last_error{GetLastError};
};

inline ImgSettingsInitializationResult InitializeImgSettingsTempDirectory(const ImgSettingsOperations& operations)
{
    wchar_t temp_path_buffer[MAX_PATH + 1]{};

    const auto path_length = operations.get_temp_path(MAX_PATH, temp_path_buffer);
    if (path_length == 0)
    {
        const auto error = operations.get_last_error();
        return {ImgSettingsStatus::TempPathFailed,
                HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_PATH_NOT_FOUND : error),
                {}};
    }
    if (path_length >= MAX_PATH)
    {
        return {ImgSettingsStatus::TempPathFailed, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER), {}};
    }

    std::wstring temp_path = temp_path_buffer;
    if (temp_path.back() != L'\\')
    {
        temp_path += L'\\';
    }

    constexpr unsigned int kMaximumDirectoryNameAttempts = 64;
    for (unsigned int attempt = 0; attempt < kMaximumDirectoryNameAttempts; ++attempt)
    {
        GUID guid{};
        const auto guid_result = operations.create_guid(&guid);
        if (FAILED(guid_result))
        {
            return {ImgSettingsStatus::GuidCreationFailed, guid_result, {}};
        }

        std::wstringstream name;
        name << std::setw(8) << std::setfill(L'0') << std::uppercase << std::hex << guid.Data1;
        const auto candidate = temp_path + name.str();
        if (operations.create_directory(candidate.c_str(), nullptr))
        {
            return {ImgSettingsStatus::Succeeded, S_OK, candidate};
        }

        const auto error = operations.get_last_error();
        if (error != ERROR_ALREADY_EXISTS)
        {
            return {ImgSettingsStatus::TempDirectoryCreationFailed, HRESULT_FROM_WIN32(error), {}};
        }
    }

    return {ImgSettingsStatus::TempDirectoryCreationFailed, HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), {}};
}

class ImgSettings
{
  public:
    static constexpr auto kAppDataPath = L"A611FF5773EC43EC\\ImgVw";

    static ImgSettings& GetInstance()
    {
        static ImgSettings settings;
        return settings;
    }
    const std::wstring& temppath() const
    {
        return initialization_result_.temp_path;
    }
    const ImgSettingsInitializationResult& initialization_result() const
    {
        return initialization_result_;
    }
    ~ImgSettings()
    {
        if (!initialization_result_.temp_path.empty())
        {
            RemoveDirectoryW(initialization_result_.temp_path.c_str());
        }
    }
    ImgSettings(const ImgSettings&) = delete;
    ImgSettings& operator=(const ImgSettings&) = delete;

  private:
    ImgSettings() : initialization_result_(InitializeImgSettingsTempDirectory(ImgSettingsOperations{})) {}

    ImgSettingsInitializationResult initialization_result_;
};

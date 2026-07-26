#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

enum class ImgSettingsStatus
{
    TempPathFailed,
    GuidCreationFailed,
    TempDirectoryCreationFailed
};

class ImgSettingsError final : public std::runtime_error
{
  public:
    ImgSettingsError(ImgSettingsStatus status, HRESULT system_error)
        : std::runtime_error(BuildMessage(status, system_error)), status_(status), system_error_(system_error)
    {
    }

    ImgSettingsStatus status() const
    {
        return status_;
    }

    HRESULT system_error() const
    {
        return system_error_;
    }

  private:
    static std::string BuildMessage(ImgSettingsStatus status, HRESULT system_error)
    {
        const char* operation = "Unknown";
        switch (status)
        {
        case ImgSettingsStatus::TempPathFailed:
            operation = "GetTempPath";
            break;
        case ImgSettingsStatus::GuidCreationFailed:
            operation = "CoCreateGuid";
            break;
        case ImgSettingsStatus::TempDirectoryCreationFailed:
            operation = "CreateDirectory";
            break;
        }

        std::stringstream message;
        message << "ImgSettings." << operation << "() failed with error 0x" << std::hex << std::setw(8)
                << std::setfill('0') << std::uppercase << static_cast<unsigned long>(system_error);
        return message.str();
    }

    ImgSettingsStatus status_;
    HRESULT system_error_;
};

class ImgSettings
{
  public:
    static constexpr auto kAppDataPath = L"A611FF5773EC43EC\\ImgVw";

  public:
    static ImgSettings& GetInstance()
    {
        static ImgSettings settings;

        return settings;
    }
    std::wstring temppath() const
    {
        return temppath_;
    }
    ~ImgSettings()
    {
        DeleteTempPath();
    }
    ImgSettings(const ImgSettings&) = delete;
    ImgSettings& operator=(const ImgSettings&) = delete;

  private:
    std::wstring temppath_;

  private:
    ImgSettings()
    {
        InitializeTempPath();
    }
    void InitializeTempPath();
    void DeleteTempPath();
};

inline void ImgSettings::InitializeTempPath()
{
    TCHAR temppathbuffer[MAX_PATH + 1]{};

    const auto pathlen = GetTempPath(MAX_PATH, temppathbuffer);
    if (pathlen == 0)
    {
        const auto error = GetLastError();
        throw ImgSettingsError(ImgSettingsStatus::TempPathFailed,
                               HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_PATH_NOT_FOUND : error));
    }
    if (pathlen >= MAX_PATH)
    {
        throw ImgSettingsError(ImgSettingsStatus::TempPathFailed, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    }

    std::wstring temppath = temppathbuffer;

    if (pathlen > 0 && temppathbuffer[pathlen - 1] != L'\\')
    {
        temppath += L'\\';
    }

    for (;;)
    {
        GUID guid{};
        const auto guidresult = CoCreateGuid(&guid);
        if (FAILED(guidresult))
        {
            throw ImgSettingsError(ImgSettingsStatus::GuidCreationFailed, guidresult);
        }

        std::wstringstream wss;
        wss << std::setw(8) << std::setfill(L'0') << std::uppercase << std::hex << guid.Data1;
        const auto testpath = temppath + wss.str();
        if (CreateDirectory(testpath.c_str(), nullptr))
        {
            temppath_ = testpath;
            return;
        }

        const auto error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS)
        {
            throw ImgSettingsError(ImgSettingsStatus::TempDirectoryCreationFailed, HRESULT_FROM_WIN32(error));
        }
    }
}

inline void ImgSettings::DeleteTempPath()
{
    if (!temppath_.empty())
    {
        RemoveDirectory(temppath_.c_str());
    }
}

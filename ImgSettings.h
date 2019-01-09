#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <string>
#include <iomanip>
#include <sstream>

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
    std::wstring temppath() const { return temppath_; }
    ~ImgSettings()
    {
        DeleteTempPath();
    }
    ImgSettings(const ImgSettings&) = delete;
    ImgSettings& operator=(const ImgSettings&) = delete;
private:
    std::wstring temppath_;
private:
    ImgSettings() { InitializeTempPath(); }
    void InitializeTempPath();
    void DeleteTempPath();
};

inline void ImgSettings::InitializeTempPath()
{
    TCHAR temppathbuffer[MAX_PATH + 1];

    const auto pathlen = GetTempPath(MAX_PATH, temppathbuffer);
    if (pathlen > MAX_PATH || pathlen == 0)
    {
        // TODO: handle error.
    }

    std::wstring temppath = temppathbuffer;

    if (pathlen > 0 && temppathbuffer[pathlen - 1] != L'\\')
    {
        temppath += L'\\';
    }

    std::wstring imgtemppath = temppath;
    std::wstring testpath;
    WIN32_FIND_DATA finddata{};
    HANDLE find{};
    BOOL folderexists = TRUE;

    do
    {
        GUID guid;
        if (CoCreateGuid(&guid) != S_OK)
        {
            // TODO: handle error.
        }

        std::wstringstream wss;
        wss << std::setw(8) << std::setfill(L'0') << std::uppercase << std::hex << guid.Data1;
        testpath = temppath + wss.str();

        find = FindFirstFile(testpath.c_str(), &finddata);
        if (find == INVALID_HANDLE_VALUE)
        {
            CreateDirectory(testpath.c_str(), NULL);
            folderexists = FALSE;
            temppath_ = testpath;
        }
        else
        {
            FindClose(find);
        }
    }
    while (folderexists);
}

inline void ImgSettings::DeleteTempPath()
{
    RemoveDirectory(temppath_.c_str());
}
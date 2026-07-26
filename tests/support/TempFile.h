#pragma once

#include <string>
#include <vector>

std::wstring TempPath(const wchar_t* filename);
void WriteBytes(const std::wstring& path, const std::vector<unsigned char>& bytes);

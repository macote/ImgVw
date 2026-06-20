#include "ImgHEIFItem.h"

#include <fstream>
#include <iostream>
#include <vector>

namespace
{
bool WriteBitmap(const wchar_t* path, ImgItem* image)
{
    auto bitmap = image->GetDisplayBitmap();
    BITMAP object{};
    if (GetObject(bitmap.bitmap(), sizeof(object), &object) != sizeof(object))
    {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = object.bmWidth;
    info.bmiHeader.biHeight = object.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    const auto stride = ((object.bmWidth * 3) + 3) & ~3;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) * object.bmHeight);

    const auto dc = GetDC(nullptr);
    const auto lines = GetDIBits(dc, bitmap.bitmap(), 0, object.bmHeight, pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != object.bmHeight)
    {
        return false;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file_header.bfSize = file_header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    return output.good();
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::wcerr << L"Usage: imgheifitem-decode <image.heic> [output.bmp]\n";
        return 2;
    }

    ImgHEIFItem image(argv[1], 640, 480);
    image.Load();
    if (image.status() != ImgItem::Status::Ready)
    {
        std::wcerr << image.errormessage() << L'\n';
        return 1;
    }
    if (WaitForSingleObject(image.loadedevent(), 0) != WAIT_OBJECT_0)
    {
        std::wcerr << L"Load completion event was not signaled.\n";
        return 1;
    }

    std::wcout << image.displaywidth() << L'x' << image.displayheight() << L'\n';
    if (argc == 3 && !WriteBitmap(argv[2], &image))
    {
        std::wcerr << L"Could not write output bitmap.\n";
        return 1;
    }
    return 0;
}

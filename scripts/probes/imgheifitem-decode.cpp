#include "ImgHEIFItem.h"

#include <iostream>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: imgheifitem-decode <image.heic>\n";
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
    return 0;
}

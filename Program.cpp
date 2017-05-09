#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include "ImgVw.h"
#include <Windows.h>

#if _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#if _MSC_VER
#pragma warning(suppress: 28251)
#ifndef _iob_defined
#define _iob_defined
FILE _iob[] = { *stdin, *stdout, *stderr };
extern "C" FILE * __cdecl __iob_func(void) { return _iob; }
#endif
#endif

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nShowCmd)
{
    auto returnvalue = ImgVw::Run(hInstance, nShowCmd);

#if _DEBUG
    _CrtDumpMemoryLeaks();
#endif

    return returnvalue;
}

#include "TestSuites.h"

void RunImgVwTests()
{
    RunPlatformTests();
    RunImgFileListTests();
    RunConcurrencyTests();
    RunImagePolicyTests();
    RunImageTests();
    RunUiTests();
}

#include "../TestSuites.h"

void RunCoreTests()
{
    RunImgFileListTests();
    RunImagePolicyTests();
    RunEmptyStateViewTests();
    RunWindowPresentationTests();
}

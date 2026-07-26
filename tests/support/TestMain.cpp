#include "TestHarness.h"
#include "../TestSuites.h"

#include <iostream>

#if !defined(IMGVW_TEST_ENTRY)
#define IMGVW_TEST_ENTRY RunImgVwTests
#endif

int main()
{
    IMGVW_TEST_ENTRY();

    const auto failures = TestFailureCount();
    if (failures != 0)
    {
        std::cerr << failures << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}

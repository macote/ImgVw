#include "TestHarness.h"

#include <iostream>

namespace
{
int failures = 0;
}

void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

int TestFailureCount()
{
    return failures;
}

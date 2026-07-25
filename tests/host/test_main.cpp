/*
 * test_main.cpp  --  Entry point for the host test runner.
 *
 * All test cases self-register via the TEST_CASE macro (see
 * test_framework.hpp), so this file only needs to kick off the run. Every
 * source file under tests/unit and tests/integration is linked into the same
 * binary and contributes its cases to the shared registry.
 */
#include "test_framework.hpp"

int main()
{
    std::printf("MC75 HomeBox Client - host test suite\n");
    std::printf("=====================================\n\n");
    return hbxtest::run_all();
}

/*
 * test_framework.hpp  --  Tiny zero-dependency test harness
 * ---------------------------------------------------------
 * A minimal assertion framework used by the host unit / integration tests.
 * No external libraries, no STL containers required in the tests themselves,
 * so it stays close in spirit to the embedded C++03 codebase under test.
 *
 * Usage:
 *   TEST_CASE("does something") {
 *       CHECK(1 + 1 == 2);
 *       CHECK_EQ_INT(answer, 42);
 *       CHECK_EQ_STR(str, "expected");
 *   }
 *   int main() { return hbxtest::run_all(); }
 *
 * Test cases self-register via a static initializer, so simply linking a file
 * of TEST_CASE blocks into the runner is enough.
 */
#ifndef HBX_TEST_FRAMEWORK_HPP
#define HBX_TEST_FRAMEWORK_HPP

#include <cstdio>
#include <cstring>

namespace hbxtest {

typedef void (*TestFn)();

struct TestCase {
    const char* name;
    TestFn      fn;
    TestCase*   next;
};

// Global intrusive list of registered test cases + running counters.
inline TestCase*& registry() { static TestCase* head = 0; return head; }
inline int& checks_run()     { static int n = 0; return n; }
inline int& checks_failed()  { static int n = 0; return n; }
inline int& current_failed() { static int n = 0; return n; }

struct Registrar {
    TestCase tc;
    Registrar(const char* name, TestFn fn) {
        tc.name = name;
        tc.fn = fn;
        tc.next = registry();
        registry() = &tc;
    }
};

inline void report(bool ok, const char* expr, const char* file, int line) {
    ++checks_run();
    if (!ok) {
        ++checks_failed();
        ++current_failed();
        std::printf("    FAIL  %s:%d  %s\n", file, line, expr);
    }
}

inline void report_eq_int(long actual, long expected, const char* expr,
                          const char* file, int line) {
    ++checks_run();
    if (actual != expected) {
        ++checks_failed();
        ++current_failed();
        std::printf("    FAIL  %s:%d  %s  (got %ld, expected %ld)\n",
                    file, line, expr, actual, expected);
    }
}

inline void report_eq_str(const char* actual, const char* expected,
                          const char* expr, const char* file, int line) {
    ++checks_run();
    bool ok = actual && expected && std::strcmp(actual, expected) == 0;
    if (!ok) {
        ++checks_failed();
        ++current_failed();
        std::printf("    FAIL  %s:%d  %s  (got \"%s\", expected \"%s\")\n",
                    file, line, expr,
                    actual ? actual : "(null)",
                    expected ? expected : "(null)");
    }
}

inline int run_all() {
    int total = 0, passed = 0;
    // Registry is built in reverse; that is fine, order is not significant.
    for (TestCase* t = registry(); t; t = t->next) {
        ++total;
        current_failed() = 0;
        std::printf("[ RUN  ] %s\n", t->name);
        t->fn();
        if (current_failed() == 0) {
            ++passed;
            std::printf("[  OK  ] %s\n", t->name);
        } else {
            std::printf("[ FAIL ] %s (%d check(s) failed)\n", t->name, current_failed());
        }
    }
    std::printf("\n==== %d/%d test cases passed, %d/%d checks passed ====\n",
                passed, total,
                checks_run() - checks_failed(), checks_run());
    return (passed == total && checks_failed() == 0) ? 0 : 1;
}

} // namespace hbxtest

#define HBX_CONCAT_(a, b) a##b
#define HBX_CONCAT(a, b)  HBX_CONCAT_(a, b)

#define TEST_CASE(NAME)                                                       \
    static void HBX_CONCAT(hbx_test_fn_, __LINE__)();                         \
    static ::hbxtest::Registrar HBX_CONCAT(hbx_test_reg_, __LINE__)(          \
        NAME, &HBX_CONCAT(hbx_test_fn_, __LINE__));                           \
    static void HBX_CONCAT(hbx_test_fn_, __LINE__)()

#define CHECK(expr) \
    ::hbxtest::report((expr) ? true : false, #expr, __FILE__, __LINE__)

#define CHECK_FALSE(expr) \
    ::hbxtest::report((expr) ? false : true, "!(" #expr ")", __FILE__, __LINE__)

#define CHECK_EQ_INT(actual, expected) \
    ::hbxtest::report_eq_int((long)(actual), (long)(expected), \
        #actual " == " #expected, __FILE__, __LINE__)

#define CHECK_EQ_STR(actual, expected) \
    ::hbxtest::report_eq_str((actual), (expected), \
        #actual " == " #expected, __FILE__, __LINE__)

#endif /* HBX_TEST_FRAMEWORK_HPP */

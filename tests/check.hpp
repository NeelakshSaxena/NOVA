#pragma once

// Minimal dependency-free test helpers shared by the test executables.
#include <iostream>
#include <string>

namespace nova_test {

inline int& failures() {
    static int n = 0;
    return n;
}

inline void check(bool cond, const std::string& what, const char* file, int line) {
    if (!cond) {
        std::cerr << file << ":" << line << ": FAIL: " << what << "\n";
        ++failures();
    }
}

template <typename A, typename B>
inline void check_eq(const A& actual, const B& expected, const std::string& what, const char* file, int line) {
    if (!(actual == expected)) {
        std::cerr << file << ":" << line << ": FAIL: " << what << "\n  expected: " << expected
                  << "\n  actual:   " << actual << "\n";
        ++failures();
    }
}

inline int finish(const char* suite) {
    if (failures() == 0) {
        std::cout << "all " << suite << " tests passed\n";
        return 0;
    }
    std::cerr << failures() << " check(s) failed in " << suite << "\n";
    return 1;
}

}  // namespace nova_test

#define CHECK(cond) ::nova_test::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) ::nova_test::check((cond), (msg), __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) ::nova_test::check_eq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)

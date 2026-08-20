/*
 * check.h - a three-function test harness
 *
 * Deliberately not a framework. These tests exist to pin down terminal
 * behaviour that is easy to break and tedious to verify by hand, and they need
 * nothing more than a counter and a printf.
 */

#ifndef RATTY_TESTS_CHECK_H
#define RATTY_TESTS_CHECK_H

#include <cstdio>
#include <string>

namespace check {

inline int failures = 0;
inline const char* currentSection = "";

inline void section(const char* name) {
    currentSection = name;
    std::printf("\n%s\n", name);
}

inline void that(bool condition, const std::string& what) {
    if (!condition) ++failures;
    std::printf("  %-4s %s\n", condition ? "ok" : "FAIL", what.c_str());
}

/* Renders a value for a failure message. std::to_string covers the arithmetic
 * types; strings are already printable. */
inline std::string describe(const std::string& value) { return "\"" + value + "\""; }
template <typename T>
inline std::string describe(const T& value) { return std::to_string(value); }

template <typename T, typename U>
inline void equal(const T& actual, const U& expected, const std::string& what) {
    const bool ok = (actual == expected);
    if (!ok) ++failures;
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) {
        std::printf("       expected %s, got %s\n",
                    describe(expected).c_str(), describe(actual).c_str());
    }
}

inline int report(const char* suite) {
    std::printf("\n%s: %s (%d failure%s)\n", suite, failures ? "FAILED" : "passed",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

} // namespace check

#endif /* RATTY_TESTS_CHECK_H */

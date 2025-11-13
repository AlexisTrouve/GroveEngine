#pragma once
#include <iostream>
#include <cstdlib>
#include <cmath>

// Couleurs pour output
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RESET   "\033[0m"

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_FALSE(condition, message) \
    ASSERT_TRUE(!(condition), message)

#define ASSERT_EQ(actual, expected, message) \
    do { \
        if ((actual) != (expected)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: " << (expected) << "\n"; \
            std::cerr << "   Actual:   " << (actual) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_NE(actual, expected, message) \
    do { \
        if ((actual) == (expected)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Should not equal: " << (expected) << "\n"; \
            std::cerr << "   But got:          " << (actual) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_LT(value, max, message) \
    do { \
        if ((value) >= (max)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: < " << (max) << "\n"; \
            std::cerr << "   Actual:   " << (value) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_GT(value, min, message) \
    do { \
        if ((value) <= (min)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: > " << (min) << "\n"; \
            std::cerr << "   Actual:   " << (value) << "\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#define ASSERT_WITHIN(actual, expected, tolerance, message) \
    do { \
        auto diff = std::abs((actual) - (expected)); \
        if (diff > (tolerance)) { \
            std::cerr << COLOR_RED << "❌ ASSERTION FAILED: " << message << COLOR_RESET << "\n"; \
            std::cerr << "   Expected: " << (expected) << " ± " << (tolerance) << "\n"; \
            std::cerr << "   Actual:   " << (actual) << " (diff: " << diff << ")\n"; \
            std::cerr << "   At: " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while(0)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Colors for test output
#define MANSI_COLOR_RED     "\x1b[31m"
#define MANSI_COLOR_GREEN   "\x1b[32m"
#define MANSI_COLOR_RESET   "\x1b[0m"

// Counter for total tests run
static int m_total_tests = 0;
static int m_passed_tests = 0;

// Print test results
#define MTEST_PRINT_RESULTS() \
    MLogf("\nTest Results: %d/%d passed\n", m_passed_tests, m_total_tests)

// Assert equality for integers
#define MASSERT_INT_EQ(actual, expected) do { \
    m_total_tests++; \
    if ((expected) == (actual)) { \
        m_passed_tests++; \
        MLogf(MANSI_COLOR_GREEN "PASS" MANSI_COLOR_RESET ": %s:%d - Expected %d, got %d", \
               __FILE__, __LINE__, (expected), (actual)); \
    } else { \
        MLogf(MANSI_COLOR_RED "FAIL" MANSI_COLOR_RESET ": %s:%d - Expected %d, got %d", \
               __FILE__, __LINE__, (expected), (actual)); \
    } \
} while(0)

// Assert equality for strings
#define MASSERT_STR_EQUALS(expected, actual) do { \
    m_total_tests++; \
    if (strcmp((expected), (actual)) == 0) { \
        m_passed_tests++; \
        MLogf(MANSI_COLOR_GREEN "PASS" MANSI_COLOR_RESET ": %s:%d - Expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, (expected), (actual)); \
    } else { \
        MLogf(MANSI_COLOR_RED "FAIL" MANSI_COLOR_RESET ": %s:%d - Expected \"%s\", got \"%s\"\n", \
               __FILE__, __LINE__, (expected), (actual)); \
    } \
} while(0)

// Assert true
#define MASSERT_TRUE(condition) do { \
    m_total_tests++; \
    if (condition) { \
        m_passed_tests++; \
        MLogf(MANSI_COLOR_GREEN "PASS" MANSI_COLOR_RESET ": %s:%d - Condition is true\n", \
               __FILE__, __LINE__); \
    } else { \
        MLogf(MANSI_COLOR_RED "FAIL" MANSI_COLOR_RESET ": %s:%d - Expected true, got false\n", \
               __FILE__, __LINE__); \
    } \
} while(0)

// Assert false
#define MASSERT_FALSE(condition) do { \
    m_total_tests++; \
    if (!(condition)) { \
        m_passed_tests++; \
        MLogf(MANSI_COLOR_GREEN "PASS" MANSI_COLOR_RESET ": %s:%d - Condition is false\n", \
               __FILE__, __LINE__); \
    } else { \
        MLogf(MANSI_COLOR_RED "FAIL" MANSI_COLOR_RESET ": %s:%d - Expected false, got true\n", \
               __FILE__, __LINE__); \
    } \
} while(0)

// Assert floating point equality within epsilon
#define MASSERT_FLOAT(expected, actual, epsilon) do { \
    m_total_tests++; \
    float diff = (expected) - (actual); \
    if (diff < 0) diff = -diff; \
    if (diff <= epsilon) { \
        m_passed_tests++; \
        MLogf(MANSI_COLOR_GREEN "PASS" MANSI_COLOR_RESET ": %s:%d - Expected %f, got %f (within %f)\n", \
               __FILE__, __LINE__, (float)(expected), (float)(actual), (float)(epsilon)); \
    } else { \
        MLogf(MANSI_COLOR_RED "FAIL" MANSI_COLOR_RESET ": %s:%d - Expected %f, got %f (epsilon %f)\n", \
               __FILE__, __LINE__, (float)(expected), (float)(actual), (float)(epsilon)); \
    } \
} while(0)

#define MTEST_FUNC(x) { MLogf(">>> %s", #x); (x); }

/* Simple test framework for Beerlang
 * Inspired by minunit - minimal, clear, effective
 */

#ifndef BEERLANG_TEST_H
#define BEERLANG_TEST_H

#include <stdio.h>
#include <string.h>

/* Test statistics */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Color output (optional) */
#define COLOR_GREEN "\x1b[32m"
#define COLOR_RED "\x1b[31m"
#define COLOR_RESET "\x1b[0m"

/* Test macros */
#define TEST(name) \
    static const char* test_##name(void)

#define ASSERT(test, message) \
    do { \
        tests_run++; \
        if (!(test)) { \
            tests_failed++; \
            printf(COLOR_RED "  FAIL: %s\n" COLOR_RESET, message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            return message; \
        } else { \
            tests_passed++; \
        } \
    } while (0)

#define ASSERT_EQ(actual, expected, message) \
    do { \
        tests_run++; \
        if ((actual) != (expected)) { \
            tests_failed++; \
            printf(COLOR_RED "  FAIL: %s\n" COLOR_RESET, message); \
            printf("    Expected: %lld, Got: %lld\n", (long long)(expected), (long long)(actual)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            return message; \
        } else { \
            tests_passed++; \
        } \
    } while (0)

#define ASSERT_STR_EQ(actual, expected, message) \
    do { \
        tests_run++; \
        if (strcmp((actual), (expected)) != 0) { \
            tests_failed++; \
            printf(COLOR_RED "  FAIL: %s\n" COLOR_RESET, message); \
            printf("    Expected: \"%s\", Got: \"%s\"\n", (expected), (actual)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            return message; \
        } else { \
            tests_passed++; \
        } \
    } while (0)

#define RUN_TEST(test) \
    do { \
        const char* message = test_##test(); \
        if (message) return message; \
        printf(COLOR_GREEN "  PASS: " COLOR_RESET #test "\n"); \
    } while (0)

/* Test suite runner */
#define RUN_SUITE(suite_name) \
    do { \
        printf("\n" #suite_name "\n"); \
        printf("==========================================\n"); \
        const char* result = suite_name(); \
        if (result) { \
            printf("\n" COLOR_RED "FAILED: %s\n" COLOR_RESET, result); \
            printf("Tests run: %d, Passed: %d, Failed: %d\n", \
                   tests_run, tests_passed, tests_failed); \
            return 1; \
        } \
        printf("\n" COLOR_GREEN "ALL TESTS PASSED\n" COLOR_RESET); \
        printf("Tests run: %d\n", tests_run); \
    } while (0)

#endif /* BEERLANG_TEST_H */

/*
 * test_framework.h — Minimal dependency-free test harness for SmartShell.
 */

#ifndef SMARTSHELL_TEST_FRAMEWORK_H
#define SMARTSHELL_TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_INIT() do { \
    g_tests_run = 0; \
    g_tests_passed = 0; \
    g_tests_failed = 0; \
} while (0)

#define RUN_TEST(test_fn) do { \
    printf("  RUN  %s\n", #test_fn); \
    g_tests_run++; \
    int failed_before = g_tests_failed; \
    test_fn(); \
    if (g_tests_failed == failed_before) { \
        g_tests_passed++; \
        printf("  PASS %s\n", #test_fn); \
    } else { \
        printf("  FAIL %s\n", #test_fn); \
    } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ_INT(expected, actual) do { \
    long exp_val = (long)(expected); \
    long act_val = (long)(actual); \
    if (exp_val != act_val) { \
        fprintf(stderr, "    ASSERT_EQ_INT failed at %s:%d: expected %ld, got %ld\n", \
                __FILE__, __LINE__, exp_val, act_val); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_STR(expected, actual) do { \
    const char *exp_s = (expected); \
    const char *act_s = (actual); \
    if (exp_s == NULL && act_s == NULL) { \
        /* both null, ok */ \
    } else if (exp_s == NULL || act_s == NULL || strcmp(exp_s, act_s) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR failed at %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, exp_s ? exp_s : "(null)", act_s ? act_s : "(null)"); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "    ASSERT_NOT_NULL failed at %s:%d: %s is NULL\n", \
                __FILE__, __LINE__, #ptr); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "    ASSERT_NULL failed at %s:%d: %s is not NULL\n", \
                __FILE__, __LINE__, #ptr); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_REPORT() do { \
    printf("\n=== Test Results: %d run, %d passed, %d failed ===\n\n", \
           g_tests_run, g_tests_passed, g_tests_failed); \
    return g_tests_failed > 0 ? 1 : 0; \
} while (0)

#endif /* SMARTSHELL_TEST_FRAMEWORK_H */

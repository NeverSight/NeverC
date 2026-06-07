/*
 * NeverC cmp module tests.
 * Validates Compare/Less semantics with NaN handling (Go cmp semantics).
 */
#include "neverc/cmp.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (expr); int _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d\n", #expr, _v, _e); } \
} while(0)

#define ASSERT_DBL_EQ(expr, expected) do { \
    double _v = (expr); double _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %g, expected %g\n", #expr, _v, _e); } \
} while(0)

#define ASSERT_TRUE(expr) ASSERT_INT_EQ(!!(expr), 1)
#define ASSERT_FALSE(expr) ASSERT_INT_EQ(!!(expr), 0)

static const double NaN = 0.0 / 0.0;
static const double Inf = 1.0 / 0.0;

static void test_compare_int(void) {
    printf("[compare_int]\n");
    ASSERT_INT_EQ(neverc_cmp_compare_int(1, 2), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_int(2, 1), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_int(5, 5), 0);
    ASSERT_INT_EQ(neverc_cmp_compare_int(-1, 0), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_int(0, -1), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_int(0, 0), 0);
    ASSERT_INT_EQ(neverc_cmp_compare_int(-2147483647-1, 2147483647), -1);
}

static void test_compare_int64(void) {
    printf("[compare_int64]\n");
    ASSERT_INT_EQ(neverc_cmp_compare_int64(100LL, 200LL), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_int64(200LL, 100LL), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_int64(0LL, 0LL), 0);
    ASSERT_INT_EQ(neverc_cmp_compare_int64(-9223372036854775807LL-1, 9223372036854775807LL), -1);
}

static void test_compare_uint64(void) {
    printf("[compare_uint64]\n");
    ASSERT_INT_EQ(neverc_cmp_compare_uint64(0ULL, 1ULL), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_uint64(1ULL, 0ULL), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_uint64(18446744073709551615ULL, 0ULL), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_uint64(42ULL, 42ULL), 0);
}

static void test_compare_float64(void) {
    printf("[compare_float64]\n");
    ASSERT_INT_EQ(neverc_cmp_compare_float64(1.0, 2.0), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(2.0, 1.0), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(3.14, 3.14), 0);
    /* NaN semantics: NaN < everything, NaN == NaN */
    ASSERT_INT_EQ(neverc_cmp_compare_float64(NaN, 1.0), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(1.0, NaN), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(NaN, NaN), 0);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(NaN, -Inf), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(-0.0, 0.0), 0);
    ASSERT_INT_EQ(neverc_cmp_compare_float64(-Inf, Inf), -1);
}

static void test_compare_float32(void) {
    printf("[compare_float32]\n");
    ASSERT_INT_EQ(neverc_cmp_compare_float32(1.0f, 2.0f), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_float32(NaN, 1.0f), -1);
    ASSERT_INT_EQ(neverc_cmp_compare_float32(1.0f, NaN), +1);
    ASSERT_INT_EQ(neverc_cmp_compare_float32(NaN, NaN), 0);
}

static void test_less_float64(void) {
    printf("[less_float64]\n");
    ASSERT_TRUE(neverc_cmp_less_float64(1.0, 2.0));
    ASSERT_FALSE(neverc_cmp_less_float64(2.0, 1.0));
    ASSERT_FALSE(neverc_cmp_less_float64(1.0, 1.0));
    ASSERT_TRUE(neverc_cmp_less_float64(NaN, 1.0));
    ASSERT_TRUE(neverc_cmp_less_float64(NaN, -Inf));
    ASSERT_FALSE(neverc_cmp_less_float64(1.0, NaN));
    ASSERT_FALSE(neverc_cmp_less_float64(NaN, NaN));
}

static void test_isnan(void) {
    printf("[isnan]\n");
    ASSERT_TRUE(neverc_cmp_isnan_float64(NaN));
    ASSERT_FALSE(neverc_cmp_isnan_float64(0.0));
    ASSERT_FALSE(neverc_cmp_isnan_float64(Inf));
    ASSERT_TRUE(neverc_cmp_isnan_float32((float)NaN));
    ASSERT_FALSE(neverc_cmp_isnan_float32(0.0f));
}

static void test_min_max_int(void) {
    printf("[min_max_int]\n");
    ASSERT_INT_EQ(neverc_cmp_min_int(3, 5), 3);
    ASSERT_INT_EQ(neverc_cmp_max_int(3, 5), 5);
    ASSERT_INT_EQ(neverc_cmp_min_int(-1, 1), -1);
    ASSERT_INT_EQ(neverc_cmp_max_int(-1, 1), 1);
    ASSERT_INT_EQ(neverc_cmp_min_int(7, 7), 7);
    ASSERT_INT_EQ(neverc_cmp_max_int(7, 7), 7);
}

static void test_min_max_float64(void) {
    printf("[min_max_float64]\n");
    ASSERT_DBL_EQ(neverc_cmp_min_float64(1.0, 2.0), 1.0);
    ASSERT_DBL_EQ(neverc_cmp_max_float64(1.0, 2.0), 2.0);
    /* NaN propagation: if either arg is NaN, return NaN */
    ASSERT_TRUE(neverc_cmp_isnan_float64(neverc_cmp_min_float64(NaN, 1.0)));
    ASSERT_TRUE(neverc_cmp_isnan_float64(neverc_cmp_min_float64(1.0, NaN)));
    ASSERT_TRUE(neverc_cmp_isnan_float64(neverc_cmp_max_float64(NaN, 1.0)));
}

static void test_clamp(void) {
    printf("[clamp]\n");
    ASSERT_INT_EQ(neverc_cmp_clamp_int(5, 1, 10), 5);
    ASSERT_INT_EQ(neverc_cmp_clamp_int(-5, 1, 10), 1);
    ASSERT_INT_EQ(neverc_cmp_clamp_int(15, 1, 10), 10);
    ASSERT_INT_EQ(neverc_cmp_clamp_int(1, 1, 1), 1);

    ASSERT_DBL_EQ(neverc_cmp_clamp_float64(5.0, 1.0, 10.0), 5.0);
    ASSERT_DBL_EQ(neverc_cmp_clamp_float64(-5.0, 1.0, 10.0), 1.0);
    ASSERT_DBL_EQ(neverc_cmp_clamp_float64(15.0, 1.0, 10.0), 10.0);
    /* NaN in clamp returns x unchanged */
    ASSERT_TRUE(neverc_cmp_isnan_float64(neverc_cmp_clamp_float64(NaN, 1.0, 10.0)));
}

int main(void) {
    printf("=== NeverC cmp Tests ===\n");
    test_compare_int();
    test_compare_int64();
    test_compare_uint64();
    test_compare_float64();
    test_compare_float32();
    test_less_float64();
    test_isnan();
    test_min_max_int();
    test_min_max_float64();
    test_clamp();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

#include "neverc/std/crypto/subtle.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_mem(const char *name, const uint8_t *got, const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: memory mismatch\n", name); }
}

static void test_constant_time_compare(void) {
    printf("[constant_time_compare]\n");

    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    uint8_t c[] = {1, 2, 3, 4, 6};
    uint8_t d[] = {0, 0, 0, 0, 0};

    check_int("equal arrays", neverc_subtle_constant_time_compare(a, b, 5), 1);
    check_int("diff last byte", neverc_subtle_constant_time_compare(a, c, 5), 0);
    check_int("all different", neverc_subtle_constant_time_compare(a, d, 5), 0);
    check_int("zero length", neverc_subtle_constant_time_compare(a, c, 0), 1);
    check_int("null with zero length",
              neverc_subtle_constant_time_compare(NULL, NULL, 0), 1);
    check_int("null pointer non-zero length",
              neverc_subtle_constant_time_compare(a, NULL, 5), 0);
    check_int("single byte eq", neverc_subtle_constant_time_compare(a, b, 1), 1);
    check_int("single byte neq", neverc_subtle_constant_time_compare(a, d, 1), 0);

    /* Verify it works with all-zero and all-0xFF */
    uint8_t zeros[32], ones[32];
    memset(zeros, 0, 32);
    memset(ones, 0xFF, 32);
    check_int("all-zero self", neverc_subtle_constant_time_compare(zeros, zeros, 32), 1);
    check_int("zero vs ones", neverc_subtle_constant_time_compare(zeros, ones, 32), 0);
}

static void test_constant_time_select(void) {
    printf("[constant_time_select]\n");

    check_int("select(1,10,20)=10", neverc_subtle_constant_time_select(1, 10, 20), 10);
    check_int("select(0,10,20)=20", neverc_subtle_constant_time_select(0, 10, 20), 20);
    check_int("select(1,-5,5)=-5", neverc_subtle_constant_time_select(1, -5, 5), -5);
    check_int("select(0,-5,5)=5", neverc_subtle_constant_time_select(0, -5, 5), 5);
    check_int("select(1,0,0)=0", neverc_subtle_constant_time_select(1, 0, 0), 0);
}

static void test_constant_time_byte_eq(void) {
    printf("[constant_time_byte_eq]\n");

    check_int("0==0", neverc_subtle_constant_time_byte_eq(0, 0), 1);
    check_int("255==255", neverc_subtle_constant_time_byte_eq(255, 255), 1);
    check_int("0!=1", neverc_subtle_constant_time_byte_eq(0, 1), 0);
    check_int("1!=0", neverc_subtle_constant_time_byte_eq(1, 0), 0);
    check_int("127!=128", neverc_subtle_constant_time_byte_eq(127, 128), 0);
    check_int("42==42", neverc_subtle_constant_time_byte_eq(42, 42), 1);

    /* Exhaustive test for a subset */
    int all_correct = 1;
    for (int i = 0; i < 256; i++) {
        for (int j = 0; j < 256; j++) {
            int expected = (i == j) ? 1 : 0;
            if (neverc_subtle_constant_time_byte_eq((uint8_t)i, (uint8_t)j) != expected) {
                all_correct = 0;
                break;
            }
        }
        if (!all_correct) break;
    }
    check_int("exhaustive 256x256", all_correct, 1);
}

static void test_constant_time_eq(void) {
    printf("[constant_time_eq]\n");

    check_int("eq(0,0)", neverc_subtle_constant_time_eq(0, 0), 1);
    check_int("eq(1,1)", neverc_subtle_constant_time_eq(1, 1), 1);
    check_int("eq(-1,-1)", neverc_subtle_constant_time_eq(-1, -1), 1);
    check_int("eq(0,1)", neverc_subtle_constant_time_eq(0, 1), 0);
    check_int("eq(1,0)", neverc_subtle_constant_time_eq(1, 0), 0);
    check_int("eq(INT32_MAX,INT32_MAX)", neverc_subtle_constant_time_eq(2147483647, 2147483647), 1);
    check_int("eq(INT32_MIN,INT32_MIN)", neverc_subtle_constant_time_eq(-2147483647-1, -2147483647-1), 1);
    check_int("eq(INT32_MAX,INT32_MIN)", neverc_subtle_constant_time_eq(2147483647, -2147483647-1), 0);

    /* Regression: x^y = 0x80000000 triggered -(int32_t)d UB before fix.
       The old code had `(uint32_t)(-(int32_t)d)` which is signed integer
       overflow when d = INT32_MIN. Fixed to use uint64 subtraction. */
    check_int("eq(0x40000000,0xC0000000)", neverc_subtle_constant_time_eq(0x40000000, (int32_t)0xC0000000), 0);
    check_int("eq(0,INT32_MIN)", neverc_subtle_constant_time_eq(0, -2147483647-1), 0);
    check_int("eq(1,-1) xor=0xFFFFFFFE", neverc_subtle_constant_time_eq(1, -1), 0);
    check_int("eq(-1,1) xor=0xFFFFFFFE", neverc_subtle_constant_time_eq(-1, 1), 0);
    check_int("eq(0x7FFFFFFF,0xFFFFFFFF)", neverc_subtle_constant_time_eq(2147483647, -1), 0);
}

static void test_constant_time_copy(void) {
    printf("[constant_time_copy]\n");

    uint8_t x[5] = {1, 2, 3, 4, 5};
    uint8_t y[5] = {10, 20, 30, 40, 50};
    uint8_t orig[5] = {1, 2, 3, 4, 5};

    /* v=0: x should not change */
    neverc_subtle_constant_time_copy(0, x, y, 5);
    check_mem("copy(0) unchanged", x, orig, 5);

    /* v=1: x should become y */
    neverc_subtle_constant_time_copy(1, x, y, 5);
    check_mem("copy(1) copied", x, y, 5);

    /* Verify with zero-length */
    uint8_t z[1] = {99};
    uint8_t w[1] = {0};
    neverc_subtle_constant_time_copy(1, z, w, 0);
    check_int("copy(1,len=0) unchanged", z[0], 99);

    neverc_subtle_constant_time_copy(1, NULL, y, 0);
    neverc_subtle_constant_time_copy(1, x, NULL, 5);
    check_mem("copy null src leaves dest", x, y, 5);

    /* dst = src+1 must not clobber unread source bytes. */
    uint8_t overlap[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    neverc_subtle_constant_time_copy(1, overlap + 1, overlap, 5);
    uint8_t overlap_exp[8] = {1, 1, 2, 3, 4, 5, 7, 8};
    check_mem("copy overlapping dst=src+1", overlap, overlap_exp, 8);
}

static void test_constant_time_less_or_eq(void) {
    printf("[constant_time_less_or_eq]\n");

    check_int("0<=0", neverc_subtle_constant_time_less_or_eq(0, 0), 1);
    check_int("1<=2", neverc_subtle_constant_time_less_or_eq(1, 2), 1);
    check_int("2<=1", neverc_subtle_constant_time_less_or_eq(2, 1), 0);
    check_int("0<=1", neverc_subtle_constant_time_less_or_eq(0, 1), 1);
    check_int("1<=1", neverc_subtle_constant_time_less_or_eq(1, 1), 1);
    check_int("100<=200", neverc_subtle_constant_time_less_or_eq(100, 200), 1);
    check_int("200<=100", neverc_subtle_constant_time_less_or_eq(200, 100), 0);
    check_int("-2<=-1", neverc_subtle_constant_time_less_or_eq(-2, -1), 1);
    check_int("-1<=-2", neverc_subtle_constant_time_less_or_eq(-1, -2), 0);
    check_int("INT32_MIN<=INT32_MAX",
              neverc_subtle_constant_time_less_or_eq(
                  -2147483647 - 1, 2147483647), 1);
    check_int("INT32_MAX<=INT32_MIN",
              neverc_subtle_constant_time_less_or_eq(
                  2147483647, -2147483647 - 1), 0);
    check_int("INT32_MIN<=INT32_MIN",
              neverc_subtle_constant_time_less_or_eq(
                  -2147483647 - 1, -2147483647 - 1), 1);
}

int main(void) {
    printf("=== NeverC Subtle Library Tests ===\n\n");

    test_constant_time_compare();
    test_constant_time_select();
    test_constant_time_byte_eq();
    test_constant_time_eq();
    test_constant_time_copy();
    test_constant_time_less_or_eq();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}

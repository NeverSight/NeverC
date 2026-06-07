#include "neverc/math/big.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { tests_run++; \
    if (strcmp((a),(b))==0) tests_passed++; \
    else { tests_failed++; printf("  FAIL: \"%s\" != \"%s\" (line %d)\n", a, b, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_set_int64(void) {
    printf("[set_int64]\n");
    neverc_bigint_t a;
    neverc_bigint_init(&a);

    neverc_bigint_set_int64(&a, 42);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 42);
    ASSERT_INT_EQ(neverc_bigint_sign(&a), 1);

    neverc_bigint_set_int64(&a, -100);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), -100);
    ASSERT_INT_EQ(neverc_bigint_sign(&a), -1);

    neverc_bigint_set_int64(&a, 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 0);
    ASSERT_INT_EQ(neverc_bigint_sign(&a), 0);
    ASSERT_TRUE(neverc_bigint_is_zero(&a));

    neverc_bigint_free(&a);
}

static void test_set_string(void) {
    printf("[set_string]\n");
    neverc_bigint_t a;
    neverc_bigint_init(&a);
    char buf[256];

    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "12345", 10), 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 12345);

    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "-9876", 10), 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), -9876);

    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "FF", 16), 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 255);

    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "1010", 2), 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 10);

    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "0xff", 0), 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 255);

    neverc_bigint_set_string(&a, "999999999999999999", 10);
    neverc_bigint_string(&a, 10, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "999999999999999999");

    neverc_bigint_free(&a);
}

static void test_add(void) {
    printf("[add]\n");
    neverc_bigint_t a, b, c;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&c);

    neverc_bigint_set_int64(&a, 100);
    neverc_bigint_set_int64(&b, 200);
    neverc_bigint_add(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), 300);

    neverc_bigint_set_int64(&a, -50);
    neverc_bigint_set_int64(&b, 30);
    neverc_bigint_add(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), -20);

    neverc_bigint_set_int64(&a, 50);
    neverc_bigint_set_int64(&b, -30);
    neverc_bigint_add(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), 20);

    neverc_bigint_set_int64(&a, -50);
    neverc_bigint_set_int64(&b, -30);
    neverc_bigint_add(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), -80);

    neverc_bigint_set_int64(&a, 50);
    neverc_bigint_set_int64(&b, -50);
    neverc_bigint_add(&c, &a, &b);
    ASSERT_TRUE(neverc_bigint_is_zero(&c));

    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&c);
}

static void test_sub(void) {
    printf("[sub]\n");
    neverc_bigint_t a, b, c;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&c);

    neverc_bigint_set_int64(&a, 100);
    neverc_bigint_set_int64(&b, 30);
    neverc_bigint_sub(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), 70);

    neverc_bigint_set_int64(&a, 30);
    neverc_bigint_set_int64(&b, 100);
    neverc_bigint_sub(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), -70);

    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&c);
}

static void test_mul(void) {
    printf("[mul]\n");
    neverc_bigint_t a, b, c;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&c);
    char buf[256];

    neverc_bigint_set_int64(&a, 12345);
    neverc_bigint_set_int64(&b, 67890);
    neverc_bigint_mul(&c, &a, &b);
    neverc_bigint_string(&c, 10, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "838102050");

    neverc_bigint_set_int64(&a, -7);
    neverc_bigint_set_int64(&b, 6);
    neverc_bigint_mul(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), -42);

    neverc_bigint_set_int64(&a, 0);
    neverc_bigint_mul(&c, &a, &b);
    ASSERT_TRUE(neverc_bigint_is_zero(&c));

    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&c);
}

static void test_div(void) {
    printf("[div]\n");
    neverc_bigint_t a, b, q, r;
    neverc_bigint_init(&a); neverc_bigint_init(&b);
    neverc_bigint_init(&q); neverc_bigint_init(&r);

    neverc_bigint_set_int64(&a, 100);
    neverc_bigint_set_int64(&b, 7);
    neverc_bigint_div(&q, &r, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&q), 14);
    ASSERT_INT_EQ(neverc_bigint_int64(&r), 2);

    neverc_bigint_set_int64(&a, 999);
    neverc_bigint_set_int64(&b, 1000);
    neverc_bigint_div(&q, &r, &a, &b);
    ASSERT_TRUE(neverc_bigint_is_zero(&q));
    ASSERT_INT_EQ(neverc_bigint_int64(&r), 999);

    neverc_bigint_set_int64(&a, 1000000);
    neverc_bigint_set_int64(&b, 1000);
    neverc_bigint_div(&q, &r, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&q), 1000);
    ASSERT_TRUE(neverc_bigint_is_zero(&r));

    neverc_bigint_free(&a); neverc_bigint_free(&b);
    neverc_bigint_free(&q); neverc_bigint_free(&r);
}

static void test_cmp(void) {
    printf("[cmp]\n");
    neverc_bigint_t a, b;
    neverc_bigint_init(&a); neverc_bigint_init(&b);

    neverc_bigint_set_int64(&a, 100);
    neverc_bigint_set_int64(&b, 200);
    ASSERT_INT_EQ(neverc_bigint_cmp(&a, &b), -1);

    neverc_bigint_set_int64(&a, 200);
    neverc_bigint_set_int64(&b, 100);
    ASSERT_INT_EQ(neverc_bigint_cmp(&a, &b), 1);

    neverc_bigint_set_int64(&a, 42);
    neverc_bigint_set_int64(&b, 42);
    ASSERT_INT_EQ(neverc_bigint_cmp(&a, &b), 0);

    neverc_bigint_set_int64(&a, -10);
    neverc_bigint_set_int64(&b, 10);
    ASSERT_INT_EQ(neverc_bigint_cmp(&a, &b), -1);

    neverc_bigint_free(&a); neverc_bigint_free(&b);
}

static void test_shift(void) {
    printf("[shift]\n");
    neverc_bigint_t a, b;
    neverc_bigint_init(&a); neverc_bigint_init(&b);

    neverc_bigint_set_int64(&a, 1);
    neverc_bigint_lsh(&b, &a, 10);
    ASSERT_INT_EQ(neverc_bigint_int64(&b), 1024);

    neverc_bigint_set_int64(&a, 1024);
    neverc_bigint_rsh(&b, &a, 5);
    ASSERT_INT_EQ(neverc_bigint_int64(&b), 32);

    neverc_bigint_set_int64(&a, 1);
    neverc_bigint_lsh(&b, &a, 64);
    ASSERT_INT_EQ(neverc_bigint_bit_len(&b), 65);

    neverc_bigint_free(&a); neverc_bigint_free(&b);
}

static void test_bit_ops(void) {
    printf("[bit_ops]\n");
    neverc_bigint_t a, b, c;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&c);

    neverc_bigint_set_int64(&a, 0xFF);
    neverc_bigint_set_int64(&b, 0x0F);
    neverc_bigint_and(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), 0x0F);

    neverc_bigint_set_int64(&a, 0xF0);
    neverc_bigint_set_int64(&b, 0x0F);
    neverc_bigint_or(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), 0xFF);

    neverc_bigint_set_int64(&a, 0xFF);
    neverc_bigint_set_int64(&b, 0x0F);
    neverc_bigint_xor(&c, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&c), 0xF0);

    neverc_bigint_set_int64(&a, 5);
    ASSERT_INT_EQ(neverc_bigint_bit(&a, 0), 1);
    ASSERT_INT_EQ(neverc_bigint_bit(&a, 1), 0);
    ASSERT_INT_EQ(neverc_bigint_bit(&a, 2), 1);

    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&c);
}

static void test_string(void) {
    printf("[string]\n");
    neverc_bigint_t a;
    neverc_bigint_init(&a);
    char buf[256];

    neverc_bigint_set_int64(&a, 0);
    neverc_bigint_string(&a, 10, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0");

    neverc_bigint_set_int64(&a, 255);
    neverc_bigint_string(&a, 16, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "ff");

    neverc_bigint_set_int64(&a, -42);
    neverc_bigint_string(&a, 10, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-42");

    neverc_bigint_set_int64(&a, 10);
    neverc_bigint_string(&a, 2, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1010");

    neverc_bigint_free(&a);
}

static void test_exp_mod(void) {
    printf("[exp_mod]\n");
    neverc_bigint_t base, exp, mod, result;
    neverc_bigint_init(&base); neverc_bigint_init(&exp);
    neverc_bigint_init(&mod); neverc_bigint_init(&result);

    neverc_bigint_set_int64(&base, 2);
    neverc_bigint_set_int64(&exp, 10);
    neverc_bigint_exp(&result, &base, &exp, NULL);
    ASSERT_INT_EQ(neverc_bigint_int64(&result), 1024);

    neverc_bigint_set_int64(&base, 3);
    neverc_bigint_set_int64(&exp, 4);
    neverc_bigint_set_int64(&mod, 5);
    neverc_bigint_exp(&result, &base, &exp, &mod);
    ASSERT_INT_EQ(neverc_bigint_int64(&result), 1);

    neverc_bigint_free(&base); neverc_bigint_free(&exp);
    neverc_bigint_free(&mod); neverc_bigint_free(&result);
}

static void test_gcd(void) {
    printf("[gcd]\n");
    neverc_bigint_t a, b, g;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&g);

    neverc_bigint_set_int64(&a, 12);
    neverc_bigint_set_int64(&b, 8);
    neverc_bigint_gcd(&g, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&g), 4);

    neverc_bigint_set_int64(&a, 17);
    neverc_bigint_set_int64(&b, 13);
    neverc_bigint_gcd(&g, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&g), 1);

    neverc_bigint_set_int64(&a, 100);
    neverc_bigint_set_int64(&b, 75);
    neverc_bigint_gcd(&g, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&g), 25);

    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&g);
}

static void test_large_numbers(void) {
    printf("[large_numbers]\n");
    neverc_bigint_t a, b, c;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&c);
    char buf[1024];

    neverc_bigint_set_string(&a, "123456789012345678901234567890", 10);
    neverc_bigint_set_string(&b, "987654321098765432109876543210", 10);
    neverc_bigint_add(&c, &a, &b);
    neverc_bigint_string(&c, 10, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1111111110111111111011111111100");

    neverc_bigint_set_string(&a, "1000000000000000000000", 10);
    neverc_bigint_set_string(&b, "1", 10);
    neverc_bigint_sub(&c, &a, &b);
    neverc_bigint_string(&c, 10, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "999999999999999999999");

    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&c);
}

int main(void) {
    printf("=== NeverC math/big Tests ===\n");
    test_set_int64();
    test_set_string();
    test_add();
    test_sub();
    test_mul();
    test_div();
    test_cmp();
    test_shift();
    test_bit_ops();
    test_string();
    test_exp_mod();
    test_gcd();
    test_large_numbers();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

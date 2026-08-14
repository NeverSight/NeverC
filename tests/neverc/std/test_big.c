#include "neverc/std/math/big.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

static void test_int64_min_boundary(void) {
    printf("[int64_min_boundary]\n");
    neverc_bigint_t a;
    neverc_bigint_init(&a);
    char buf[64];

    /* INT64_MIN: magnitude 2^63 must not be reached via signed negation (UB). */
    int64_t imin = -9223372036854775807LL - 1;
    neverc_bigint_set_int64(&a, imin);
    ASSERT_TRUE(neverc_bigint_int64(&a) == imin);
    ASSERT_INT_EQ(neverc_bigint_sign(&a), -1);
    ASSERT_INT_EQ(neverc_bigint_string(&a, 10, buf, sizeof buf) >= 0, 1);
    ASSERT_STR_EQ(buf, "-9223372036854775808");

    /* Round-trip the decimal form back. */
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "-9223372036854775808", 10), 0);
    ASSERT_TRUE(neverc_bigint_int64(&a) == imin);

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

    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "0", 0), 0);
    ASSERT_TRUE(neverc_bigint_is_zero(&a));
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "0x", 0), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "0b", 0), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "0o", 0), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "+", 10), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "-", 10), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "_1", 0), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "1_", 0), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "1__2", 0), -1);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "1_000", 0), 0);
    ASSERT_INT_EQ(neverc_bigint_int64(&a), 1000);
    ASSERT_INT_EQ(neverc_bigint_set_string(&a, "1_000", 10), -1);

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

    neverc_bigint_set_int64(&a, 100);
    neverc_bigint_set_int64(&b, 7);
    neverc_bigint_div(&q, &q, &a, &b);
    ASSERT_INT_EQ(neverc_bigint_int64(&q), 14);

    neverc_bigint_set_int64(&q, 999);
    neverc_bigint_set_int64(&r, 999);
    neverc_bigint_set_int64(&b, 0);
    neverc_bigint_div(&q, &r, &a, &b);
    ASSERT_TRUE(neverc_bigint_is_zero(&q));
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

    neverc_bigint_set_int64(&base, 2);
    neverc_bigint_set_int64(&exp, -3);
    neverc_bigint_exp(&result, &base, &exp, NULL);
    ASSERT_INT_EQ(neverc_bigint_int64(&result), 1);

    neverc_bigint_t x, m, z;
    neverc_bigint_init(&x); neverc_bigint_init(&m); neverc_bigint_init(&z);
    neverc_bigint_set_int64(&x, -7);
    neverc_bigint_set_int64(&m, 3);
    neverc_bigint_mod(&z, &x, &m);
    ASSERT_INT_EQ(neverc_bigint_int64(&z), 2);
    neverc_bigint_set_int64(&x, -7);
    neverc_bigint_set_int64(&m, -3);
    neverc_bigint_mod(&z, &x, &m);
    ASSERT_INT_EQ(neverc_bigint_int64(&z), 2);
    neverc_bigint_set_int64(&x, 7);
    neverc_bigint_set_int64(&m, -3);
    neverc_bigint_mod(&z, &x, &m);
    ASSERT_INT_EQ(neverc_bigint_int64(&z), 1);

    neverc_bigint_set_int64(&x, -7);
    neverc_bigint_set_int64(&m, -3);
    neverc_bigint_mod(&m, &x, &m);
    ASSERT_INT_EQ(neverc_bigint_int64(&m), 2);
    neverc_bigint_set_int64(&x, -7);
    neverc_bigint_set_int64(&m, 3);
    neverc_bigint_mod(&x, &x, &m);
    ASSERT_INT_EQ(neverc_bigint_int64(&x), 2);

    neverc_bigint_set_int64(&base, -2);
    neverc_bigint_set_int64(&exp, 3);
    neverc_bigint_exp(&result, &base, &exp, NULL);
    ASSERT_INT_EQ(neverc_bigint_int64(&result), -8);
    neverc_bigint_set_int64(&base, -2);
    neverc_bigint_set_int64(&exp, 3);
    neverc_bigint_set_int64(&mod, 5);
    neverc_bigint_exp(&result, &base, &exp, &mod);
    ASSERT_INT_EQ(neverc_bigint_int64(&result), 2);

    neverc_bigint_free(&x); neverc_bigint_free(&m); neverc_bigint_free(&z);

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

/* ---- randomized differential tests for the optimized algorithms ----
 * The new Lehmer GCD and windowed exp are cross-checked against independent
 * reference implementations (plain Euclid / binary square-and-multiply) built
 * only from unchanged primitives (mod/mul). String parse+format is exercised by
 * round-tripping random decimal strings. */

static uint64_t rng_state = 0x123456789abcdef0ULL;
static uint32_t rng32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}

static void make_random(neverc_bigint_t *z, int nwords) {
    neverc_bigint_t w;
    neverc_bigint_init(&w);
    neverc_bigint_set_int64(z, 0);
    for (int i = 0; i < nwords; i++) {
        neverc_bigint_lsh(z, z, 32);
        neverc_bigint_set_uint64(&w, rng32());
        neverc_bigint_add(z, z, &w);
    }
    neverc_bigint_free(&w);
}

/* plain Euclid, independent of the new Lehmer GCD */
static void ref_gcd(neverc_bigint_t *g, const neverc_bigint_t *x,
                    const neverc_bigint_t *y) {
    neverc_bigint_t a, b, t;
    neverc_bigint_init(&a); neverc_bigint_init(&b); neverc_bigint_init(&t);
    neverc_bigint_abs(&a, x);
    neverc_bigint_abs(&b, y);
    while (!neverc_bigint_is_zero(&b)) {
        neverc_bigint_mod(&t, &a, &b);
        neverc_bigint_set(&a, &b);
        neverc_bigint_set(&b, &t);
    }
    neverc_bigint_set(g, &a);
    neverc_bigint_free(&a); neverc_bigint_free(&b); neverc_bigint_free(&t);
}

/* binary square-and-multiply, independent of the new windowed exp */
static void ref_exp(neverc_bigint_t *z, const neverc_bigint_t *base,
                    const neverc_bigint_t *exp, const neverc_bigint_t *m) {
    neverc_bigint_t result, b, e;
    neverc_bigint_init(&result); neverc_bigint_init(&b); neverc_bigint_init(&e);
    int domod = (m && m->len > 0);
    neverc_bigint_set_int64(&result, 1);
    neverc_bigint_abs(&b, base);
    neverc_bigint_set(&e, exp);
    if (domod) neverc_bigint_mod(&b, &b, m);
    int bits = neverc_bigint_bit_len(&e);
    for (int i = 0; i < bits; i++) {
        if (neverc_bigint_bit(&e, (unsigned)i)) {
            neverc_bigint_mul(&result, &result, &b);
            if (domod) neverc_bigint_mod(&result, &result, m);
        }
        neverc_bigint_mul(&b, &b, &b);
        if (domod) neverc_bigint_mod(&b, &b, m);
    }
    neverc_bigint_set(z, &result);
    neverc_bigint_free(&result); neverc_bigint_free(&b); neverc_bigint_free(&e);
}

static int divides(const neverc_bigint_t *g, const neverc_bigint_t *x) {
    if (neverc_bigint_is_zero(g)) return neverc_bigint_is_zero(x);
    neverc_bigint_t r;
    neverc_bigint_init(&r);
    neverc_bigint_mod(&r, x, g);
    int ok = neverc_bigint_is_zero(&r);
    neverc_bigint_free(&r);
    return ok;
}

static void test_gcd_random(void) {
    printf("[gcd_random]\n");
    rng_state = 0xdeadbeefcafef00dULL;
    neverc_bigint_t a, b, g, gref, k, prod;
    neverc_bigint_init(&a); neverc_bigint_init(&b);
    neverc_bigint_init(&g); neverc_bigint_init(&gref);
    neverc_bigint_init(&k); neverc_bigint_init(&prod);

    int mismatches = 0, divfail = 0;
    for (int it = 0; it < 1200; it++) {
        int wa = 1 + (int)(rng32() % 24);
        int wb = 1 + (int)(rng32() % 24);
        make_random(&a, wa);
        make_random(&b, wb);
        /* 1/4 of the time make a a clean multiple of b to stress exact division */
        if ((rng32() & 3) == 0 && !neverc_bigint_is_zero(&b)) {
            make_random(&k, 1 + (int)(rng32() % 6));
            neverc_bigint_mul(&prod, &b, &k);
            neverc_bigint_set(&a, &prod);
        }
        neverc_bigint_gcd(&g, &a, &b);
        ref_gcd(&gref, &a, &b);
        if (neverc_bigint_cmp(&g, &gref) != 0) mismatches++;
        if (!neverc_bigint_is_zero(&g)) {
            if (!divides(&g, &a) || !divides(&g, &b)) divfail++;
        }
    }
    ASSERT_INT_EQ(mismatches, 0);
    ASSERT_INT_EQ(divfail, 0);

    neverc_bigint_free(&a); neverc_bigint_free(&b);
    neverc_bigint_free(&g); neverc_bigint_free(&gref);
    neverc_bigint_free(&k); neverc_bigint_free(&prod);
}

static void test_gcd_fibonacci(void) {
    printf("[gcd_fibonacci]\n");
    /* Consecutive Fibonacci numbers are the Euclidean worst case; their gcd is
     * 1, which maximally exercises Lehmer's quotient batching. */
    neverc_bigint_t f0, f1, t, g, one;
    neverc_bigint_init(&f0); neverc_bigint_init(&f1);
    neverc_bigint_init(&t); neverc_bigint_init(&g); neverc_bigint_init(&one);
    neverc_bigint_set_int64(&f0, 0);
    neverc_bigint_set_int64(&f1, 1);
    neverc_bigint_set_int64(&one, 1);
    for (int i = 0; i < 4000; i++) {
        neverc_bigint_add(&t, &f0, &f1);
        neverc_bigint_set(&f0, &f1);
        neverc_bigint_set(&f1, &t);
    }
    neverc_bigint_gcd(&g, &f0, &f1);
    ASSERT_INT_EQ(neverc_bigint_cmp(&g, &one), 0);
    /* gcd(F, F) == F */
    neverc_bigint_gcd(&g, &f1, &f1);
    ASSERT_INT_EQ(neverc_bigint_cmp(&g, &f1), 0);

    neverc_bigint_free(&f0); neverc_bigint_free(&f1);
    neverc_bigint_free(&t); neverc_bigint_free(&g); neverc_bigint_free(&one);
}

static void test_exp_random(void) {
    printf("[exp_random]\n");
    rng_state = 0x5151515151515151ULL;
    neverc_bigint_t base, exp, mod, r, rref;
    neverc_bigint_init(&base); neverc_bigint_init(&exp);
    neverc_bigint_init(&mod); neverc_bigint_init(&r); neverc_bigint_init(&rref);

    int mismatches = 0;
    for (int it = 0; it < 300; it++) {
        make_random(&base, 1 + (int)(rng32() % 4));
        make_random(&exp, 1 + (int)(rng32() % 2));   /* keep exponent modest */
        make_random(&mod, 1 + (int)(rng32() % 4));
        if (neverc_bigint_is_zero(&mod)) neverc_bigint_set_int64(&mod, 1);
        /* Force odd modulus on ~half the cases to exercise the Montgomery path,
         * leave the rest as-is so the even-modulus fallback is covered too. */
        if ((it & 1) && mod.len > 0) mod.digits[0] |= 1u;
        neverc_bigint_exp(&r, &base, &exp, &mod);
        ref_exp(&rref, &base, &exp, &mod);
        if (neverc_bigint_cmp(&r, &rref) != 0) mismatches++;
    }
    /* a few non-modular cases with small exponents */
    for (int it = 0; it < 30; it++) {
        make_random(&base, 1 + (int)(rng32() % 2));
        neverc_bigint_set_int64(&exp, (int64_t)(rng32() % 40));
        neverc_bigint_exp(&r, &base, &exp, NULL);
        ref_exp(&rref, &base, &exp, NULL);
        if (neverc_bigint_cmp(&r, &rref) != 0) mismatches++;
    }
    ASSERT_INT_EQ(mismatches, 0);

    neverc_bigint_free(&base); neverc_bigint_free(&exp);
    neverc_bigint_free(&mod); neverc_bigint_free(&r); neverc_bigint_free(&rref);
}

static void test_string_roundtrip(void) {
    printf("[string_roundtrip]\n");
    rng_state = 0xabcdef0123456789ULL;
    neverc_bigint_t a;
    neverc_bigint_init(&a);
    char dec[2048], out[2048];

    int mismatches = 0;
    for (int it = 0; it < 600; it++) {
        int ndig = 1 + (int)(rng32() % 200);
        int p = 0;
        dec[p++] = (char)('1' + (rng32() % 9));        /* no leading zero */
        for (int i = 1; i < ndig; i++) dec[p++] = (char)('0' + (rng32() % 10));
        dec[p] = '\0';
        neverc_bigint_set_string(&a, dec, 10);
        neverc_bigint_string(&a, 10, out, sizeof(out));
        if (strcmp(dec, out) != 0) mismatches++;
    }
    ASSERT_INT_EQ(mismatches, 0);

    /* base-16 and base-2 round-trips */
    int basemis = 0;
    for (int it = 0; it < 200; it++) {
        int ndig = 1 + (int)(rng32() % 80);
        int p = 0;
        const char *hd = "0123456789abcdef";
        dec[p++] = hd[1 + (rng32() % 15)];
        for (int i = 1; i < ndig; i++) dec[p++] = hd[rng32() % 16];
        dec[p] = '\0';
        neverc_bigint_set_string(&a, dec, 16);
        neverc_bigint_string(&a, 16, out, sizeof(out));
        if (strcmp(dec, out) != 0) basemis++;
    }
    ASSERT_INT_EQ(basemis, 0);

    /* format anchor independent of parse: 2^256 in base 16 is 1 then 64 zeros */
    neverc_bigint_set_int64(&a, 1);
    neverc_bigint_lsh(&a, &a, 256);
    neverc_bigint_string(&a, 16, out, sizeof(out));
    {
        char expect[80];
        expect[0] = '1';
        for (int i = 0; i < 64; i++) expect[1 + i] = '0';
        expect[65] = '\0';
        ASSERT_STR_EQ(out, expect);
    }

    neverc_bigint_free(&a);
}

/* Large round-trips that exceed the divide-and-conquer base-conversion
 * threshold (thousands of digits -> hundreds of word-chunks), so the
 * subquadratic parse path is exercised and checked against the formatter. */
static void test_string_large_roundtrip(void) {
    printf("[string_large_roundtrip]\n");
    rng_state = 0x13579bdf2468aceULL;
    neverc_bigint_t a;
    neverc_bigint_init(&a);

    int mismatches = 0;
    for (int it = 0; it < 40; it++) {
        int ndig = 1000 + (int)(rng32() % 5000);       /* 1000..5999 digits */
        char *dec = (char *)malloc((size_t)ndig + 2);
        char *out = (char *)malloc((size_t)ndig + 2);
        int p = 0;
        dec[p++] = (char)('1' + (rng32() % 9));         /* no leading zero */
        for (int i = 1; i < ndig; i++) dec[p++] = (char)('0' + (rng32() % 10));
        dec[p] = '\0';
        neverc_bigint_set_string(&a, dec, 10);
        int n = neverc_bigint_string(&a, 10, out, (size_t)ndig + 2);
        if (n < 0 || strcmp(dec, out) != 0) mismatches++;
        free(dec); free(out);
    }
    ASSERT_INT_EQ(mismatches, 0);

    /* base 16 large round-trip */
    int hexmis = 0;
    const char *hd = "0123456789abcdef";
    for (int it = 0; it < 20; it++) {
        int ndig = 1000 + (int)(rng32() % 4000);
        char *hx = (char *)malloc((size_t)ndig + 2);
        char *out = (char *)malloc((size_t)ndig + 2);
        int p = 0;
        hx[p++] = hd[1 + (rng32() % 15)];
        for (int i = 1; i < ndig; i++) hx[p++] = hd[rng32() % 16];
        hx[p] = '\0';
        neverc_bigint_set_string(&a, hx, 16);
        int n = neverc_bigint_string(&a, 16, out, (size_t)ndig + 2);
        if (n < 0 || strcmp(hx, out) != 0) hexmis++;
        free(hx); free(out);
    }
    ASSERT_INT_EQ(hexmis, 0);

    /* embedded underscores are ignored, value unchanged */
    neverc_bigint_t b;
    neverc_bigint_init(&b);
    char *big = (char *)malloc(4002), *gib = (char *)malloc(8002);
    int p = 0, g = 0;
    for (int i = 0; i < 4000; i++) {
        char c = (char)('0' + (rng32() % 10));
        if (i == 0) c = (char)('1' + (rng32() % 9));
        big[p++] = c; gib[g++] = c;
        if (i % 3 == 1) gib[g++] = '_';                 /* sprinkle separators */
    }
    big[p] = '\0'; gib[g] = '\0';
    neverc_bigint_set_string(&a, big, 10);
    /* Underscores are accepted only with base 0, matching Go big.Int.SetString. */
    neverc_bigint_set_string(&b, gib, 0);
    ASSERT_TRUE(neverc_bigint_cmp(&a, &b) == 0);
    free(big); free(gib);
    neverc_bigint_free(&b);

    neverc_bigint_free(&a);
}

/* Large random division stressing the Burnikel-Ziegler path: verify the
 * defining invariant x == q*y + r with 0 <= |r| < |y| (which uniquely pins
 * down q and r) for divisor sizes well above the BZ threshold, across signs. */
static void test_div_large_random(void) {
    printf("[div_large_random]\n");
    rng_state = 0x0a1b2c3d4e5f6071ULL;
    neverc_bigint_t x, y, q, r, chk, ar, ay;
    neverc_bigint_init(&x); neverc_bigint_init(&y); neverc_bigint_init(&q);
    neverc_bigint_init(&r); neverc_bigint_init(&chk);
    neverc_bigint_init(&ar); neverc_bigint_init(&ay);

    int bad = 0;
    for (int it = 0; it < 60; it++) {
        int yb = 200 + (int)(rng32() % 400);          /* 200..599 divisor words */
        int xb = yb + 1 + (int)(rng32() % 600);       /* strictly larger dividend */
        make_random(&x, xb);
        make_random(&y, yb);
        if (neverc_bigint_is_zero(&y)) neverc_bigint_set_int64(&y, 1);
        if (it & 1) x.neg = (x.len > 0);              /* exercise sign handling */
        if (it & 2) y.neg = (y.len > 0);

        neverc_bigint_div(&q, &r, &x, &y);
        neverc_bigint_mul(&chk, &q, &y);
        neverc_bigint_add(&chk, &chk, &r);
        if (neverc_bigint_cmp(&chk, &x) != 0) bad++;  /* x == q*y + r */
        neverc_bigint_abs(&ar, &r);
        neverc_bigint_abs(&ay, &y);
        if (neverc_bigint_cmp(&ar, &ay) >= 0) bad++;  /* |r| < |y| */
    }
    ASSERT_INT_EQ(bad, 0);

    neverc_bigint_free(&x); neverc_bigint_free(&y); neverc_bigint_free(&q);
    neverc_bigint_free(&r); neverc_bigint_free(&chk);
    neverc_bigint_free(&ar); neverc_bigint_free(&ay);
}

/* Huge base-10 round-trips (tens of thousands of digits) so the formatter's
 * divide-and-conquer path drives Burnikel-Ziegler divisions internally. */
static void test_string_huge_roundtrip(void) {
    printf("[string_huge_roundtrip]\n");
    rng_state = 0x0777eee111222333ULL;
    neverc_bigint_t a, b;
    neverc_bigint_init(&a); neverc_bigint_init(&b);

    int bad = 0;
    for (int it = 0; it < 3; it++) {
        int ndig = 20000 + (int)(rng32() % 15000);    /* 20k..34k decimal digits */
        char *dec = (char *)malloc((size_t)ndig + 2);
        char *out = (char *)malloc((size_t)ndig + 2);
        int p = 0;
        dec[p++] = (char)('1' + (rng32() % 9));
        for (int i = 1; i < ndig; i++) dec[p++] = (char)('0' + (rng32() % 10));
        dec[p] = '\0';
        neverc_bigint_set_string(&a, dec, 10);
        int n = neverc_bigint_string(&a, 10, out, (size_t)ndig + 2);
        if (n < 0 || strcmp(dec, out) != 0) bad++;
        neverc_bigint_set_string(&b, out, 10);
        if (neverc_bigint_cmp(&a, &b) != 0) bad++;
        free(dec); free(out);
    }
    ASSERT_INT_EQ(bad, 0);

    neverc_bigint_free(&a); neverc_bigint_free(&b);
}

/* Large random multiply + dedicated squaring-path differential test.
 *
 * big.c takes the symmetric squaring path (nat_sqr -> Karatsuba-sqr at 80
 * words, Toom-3-sqr at 120) ONLY when both mul operands are the same pointer
 * (`if (x == y)`). The existing randomized tests reach it only through small-
 * operand modexp, so the large-operand squaring code had no direct coverage.
 * Here neverc_bigint_mul(z, a, a) drives it at word counts straddling every
 * tier, cross-checked against the independent general-mul path with identities
 * that no single-path carry/interpolation bug can satisfy simultaneously:
 *   a*a (square path) == a*acopy (general mul)   a^2 >= 0
 *   a*b == b*a                                   commutativity at Toom-3 scale
 *   (a+b)*(a-b) == a^2 - b^2                      difference of squares
 *   (a+b)^2 == a^2 + 2ab + b^2                    square of sum */
static void test_mul_sqr_large_random(void) {
    printf("[mul_sqr_large_random]\n");
    rng_state = 0x5151aaaa33337777ULL;
    neverc_bigint_t a, ac, b, aa, bb, ab, ba, t, s, d, lhs, rhs;
    neverc_bigint_init(&a);  neverc_bigint_init(&ac); neverc_bigint_init(&b);
    neverc_bigint_init(&aa); neverc_bigint_init(&bb); neverc_bigint_init(&ab);
    neverc_bigint_init(&ba); neverc_bigint_init(&t);  neverc_bigint_init(&s);
    neverc_bigint_init(&d);  neverc_bigint_init(&lhs); neverc_bigint_init(&rhs);

    int bad = 0;
    for (int it = 0; it < 40; it++) {
        /* 40..269 words straddles Karatsuba(40)/Toom-3(144) multiply and
         * Karatsuba-sqr(80)/Toom-3-sqr(120) squaring thresholds. */
        int wa = 40 + (int)(rng32() % 230);
        int wb = 40 + (int)(rng32() % 230);
        make_random(&a, wa);
        make_random(&b, wb);
        if (it & 1) a.neg = (a.len > 0);              /* sign must not affect a^2 */
        if (it & 2) b.neg = (b.len > 0);

        neverc_bigint_set(&ac, &a);                   /* distinct copy of a */
        neverc_bigint_mul(&aa, &a, &a);               /* a^2 via squaring path (x==y) */
        neverc_bigint_mul(&bb, &b, &b);               /* b^2 via squaring path */
        neverc_bigint_mul(&t,  &a, &ac);              /* a*a via general mul   (x!=y) */
        if (neverc_bigint_cmp(&t, &aa) != 0) bad++;   /* square path == general mul */
        if (neverc_bigint_sign(&aa) < 0)    bad++;    /* a^2 is nonnegative */

        neverc_bigint_mul(&ab, &a, &b);
        neverc_bigint_mul(&ba, &b, &a);
        if (neverc_bigint_cmp(&ab, &ba) != 0) bad++;  /* commutativity */

        neverc_bigint_add(&s, &a, &b);                /* a+b */
        neverc_bigint_sub(&d, &a, &b);                /* a-b */
        neverc_bigint_mul(&lhs, &s, &d);              /* (a+b)(a-b) */
        neverc_bigint_sub(&rhs, &aa, &bb);            /* a^2 - b^2 */
        if (neverc_bigint_cmp(&lhs, &rhs) != 0) bad++;

        neverc_bigint_mul(&t, &s, &s);                /* (a+b)^2 via squaring path */
        neverc_bigint_add(&rhs, &aa, &bb);            /* a^2 + b^2 */
        neverc_bigint_add(&rhs, &rhs, &ab);           /* + ab */
        neverc_bigint_add(&rhs, &rhs, &ab);           /* + ab => a^2 + 2ab + b^2 */
        if (neverc_bigint_cmp(&t, &rhs) != 0) bad++;
    }
    ASSERT_INT_EQ(bad, 0);

    neverc_bigint_free(&a);  neverc_bigint_free(&ac); neverc_bigint_free(&b);
    neverc_bigint_free(&aa); neverc_bigint_free(&bb); neverc_bigint_free(&ab);
    neverc_bigint_free(&ba); neverc_bigint_free(&t);  neverc_bigint_free(&s);
    neverc_bigint_free(&d);  neverc_bigint_free(&lhs); neverc_bigint_free(&rhs);
}

int main(void) {
    printf("=== NeverC math/big Tests ===\n");
    test_set_int64();
    test_int64_min_boundary();
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
    test_gcd_random();
    test_gcd_fibonacci();
    test_exp_random();
    test_string_roundtrip();
    test_string_large_roundtrip();
    test_div_large_random();
    test_mul_sqr_large_random();
    test_string_huge_roundtrip();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

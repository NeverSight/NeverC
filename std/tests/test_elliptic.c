#include "neverc/crypto/elliptic.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

static void test_p256_params(void) {
    printf("[p256_params]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    ASSERT_TRUE(c != NULL);
    ASSERT_INT_EQ(c->bit_size, 256);
    ASSERT_TRUE(strcmp(c->name, "P-256") == 0);
    ASSERT_TRUE(!neverc_bigint_is_zero(&c->p));
    ASSERT_TRUE(!neverc_bigint_is_zero(&c->n));
    ASSERT_TRUE(!neverc_bigint_is_zero(&c->gx));
    ASSERT_TRUE(!neverc_bigint_is_zero(&c->gy));
}

static void test_generator_on_curve(void) {
    printf("[generator_on_curve]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t g;
    neverc_elliptic_point_init(&g);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &g));
    neverc_elliptic_point_free(&g);
}

static void test_double(void) {
    printf("[double]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t g, r;
    neverc_elliptic_point_init(&g);
    neverc_elliptic_point_init(&r);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);

    neverc_elliptic_double(c, &r, &g);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &r));
    ASSERT_TRUE(neverc_bigint_cmp(&r.x, &g.x) != 0);

    neverc_elliptic_point_free(&g);
    neverc_elliptic_point_free(&r);
}

static void test_add(void) {
    printf("[add]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t g, g2, g3;
    neverc_elliptic_point_init(&g);
    neverc_elliptic_point_init(&g2);
    neverc_elliptic_point_init(&g3);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);

    neverc_elliptic_double(c, &g2, &g);
    neverc_elliptic_add(c, &g3, &g2, &g);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &g3));

    neverc_elliptic_point_free(&g);
    neverc_elliptic_point_free(&g2);
    neverc_elliptic_point_free(&g3);
}

static void test_scalar_mult(void) {
    printf("[scalar_mult]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t r;
    neverc_elliptic_point_init(&r);
    neverc_bigint_t k;
    neverc_bigint_init(&k);

    neverc_bigint_set_int64(&k, 1);
    neverc_elliptic_scalar_base_mult(c, &r, &k);
    ASSERT_TRUE(neverc_bigint_cmp(&r.x, &c->gx) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&r.y, &c->gy) == 0);

    neverc_bigint_set_int64(&k, 2);
    neverc_elliptic_scalar_base_mult(c, &r, &k);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &r));

    neverc_bigint_set_int64(&k, 7);
    neverc_elliptic_scalar_base_mult(c, &r, &k);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &r));

    neverc_elliptic_point_free(&r);
    neverc_bigint_free(&k);
}

static void test_marshal_unmarshal(void) {
    printf("[marshal_unmarshal]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t g, decoded;
    neverc_elliptic_point_init(&g);
    neverc_elliptic_point_init(&decoded);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);

    unsigned char buf[128];
    size_t len;
    ASSERT_INT_EQ(neverc_elliptic_marshal(c, &g, buf, sizeof(buf), &len), 0);
    ASSERT_INT_EQ((int)len, 65);
    ASSERT_INT_EQ(buf[0], 0x04);

    ASSERT_INT_EQ(neverc_elliptic_unmarshal(c, &decoded, buf, len), 0);
    ASSERT_TRUE(neverc_bigint_cmp(&decoded.x, &g.x) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&decoded.y, &g.y) == 0);

    neverc_elliptic_point_free(&g);
    neverc_elliptic_point_free(&decoded);
}

static void test_identity(void) {
    printf("[identity]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t g, id, r;
    neverc_elliptic_point_init(&g);
    neverc_elliptic_point_init(&id);
    neverc_elliptic_point_init(&r);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);

    neverc_elliptic_add(c, &r, &g, &id);
    ASSERT_TRUE(neverc_bigint_cmp(&r.x, &g.x) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&r.y, &g.y) == 0);

    neverc_elliptic_point_free(&g);
    neverc_elliptic_point_free(&id);
    neverc_elliptic_point_free(&r);
}

static void test_p384(void) {
    printf("[p384]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p384();
    ASSERT_TRUE(c != NULL);
    ASSERT_INT_EQ(c->bit_size, 384);

    neverc_elliptic_point_t g;
    neverc_elliptic_point_init(&g);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &g));
    neverc_elliptic_point_free(&g);
}

int main(void) {
    printf("=== NeverC crypto/elliptic Tests ===\n");
    test_p256_params();
    test_generator_on_curve();
    test_double();
    test_add();
    test_scalar_mult();
    test_marshal_unmarshal();
    test_identity();
    test_p384();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

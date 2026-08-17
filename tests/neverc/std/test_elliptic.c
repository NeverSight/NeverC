#include "neverc/std/crypto/elliptic.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

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

#define INIT_THREAD_COUNT 8

typedef struct {
    int ok;
} init_thread_context_t;

static int init_threads_start;

#ifdef _WIN32
static DWORD WINAPI init_thread_main(LPVOID argument) {
#else
static void *init_thread_main(void *argument) {
#endif
    init_thread_context_t *context = (init_thread_context_t *)argument;
    while (__atomic_load_n(&init_threads_start, __ATOMIC_ACQUIRE) == 0) {
    }
    const neverc_elliptic_curve_t *p256 = neverc_elliptic_p256();
    const neverc_elliptic_curve_t *p384 = neverc_elliptic_p384();
    context->ok = p256 && p384 && p256->bit_size == 256 &&
                  p384->bit_size == 384 &&
                  !neverc_bigint_is_zero(&p256->p) &&
                  !neverc_bigint_is_zero(&p384->p);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void test_concurrent_first_use(void) {
    printf("[concurrent_first_use]\n");
    init_thread_context_t contexts[INIT_THREAD_COUNT] = {{0}};
    int ok = 1;
#ifdef _WIN32
    HANDLE threads[INIT_THREAD_COUNT] = {0};
    for (int i = 0; i < INIT_THREAD_COUNT; ++i) {
        threads[i] = CreateThread(
            NULL, 0, init_thread_main, &contexts[i], 0, NULL);
        if (!threads[i])
            ok = 0;
    }
    __atomic_store_n(&init_threads_start, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < INIT_THREAD_COUNT; ++i) {
        if (threads[i]) {
            if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0)
                ok = 0;
            CloseHandle(threads[i]);
        }
        if (!contexts[i].ok)
            ok = 0;
    }
#else
    pthread_t threads[INIT_THREAD_COUNT];
    int created = 0;
    for (int i = 0; i < INIT_THREAD_COUNT; ++i) {
        if (pthread_create(
                &threads[i], NULL, init_thread_main, &contexts[i]) != 0)
            break;
        ++created;
    }
    if (created != INIT_THREAD_COUNT)
        ok = 0;
    __atomic_store_n(&init_threads_start, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < created; ++i) {
        if (pthread_join(threads[i], NULL) != 0 || !contexts[i].ok)
            ok = 0;
    }
#endif
    ASSERT_TRUE(ok);
}

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

static void test_invalid_affine_points(void) {
    printf("[invalid_affine_points]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t point;
    neverc_elliptic_point_init(&point);

    ASSERT_TRUE(!neverc_elliptic_is_on_curve(c, &point));
    unsigned char encoded[65] = {0x04};
    size_t length = 123;
    ASSERT_INT_EQ(neverc_elliptic_marshal(
                      c, &point, encoded, sizeof(encoded), &length),
                  -1);
    ASSERT_INT_EQ((int)length, 0);
    ASSERT_INT_EQ(neverc_elliptic_unmarshal(
                      c, &point, encoded, sizeof(encoded)),
                  -1);

    neverc_bigint_set(&point.x, &c->gx);
    neverc_bigint_set(&point.y, &c->gy);
    unsigned char off_curve[65] = {0x04};
    ASSERT_INT_EQ(neverc_elliptic_unmarshal(
                      c, &point, off_curve, sizeof(off_curve)),
                  -1);
    ASSERT_TRUE(neverc_bigint_cmp(&point.x, &c->gx) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&point.y, &c->gy) == 0);

    neverc_bigint_set(&point.x, &c->p);
    neverc_bigint_set_int64(&point.y, 1);
    ASSERT_TRUE(!neverc_elliptic_is_on_curve(c, &point));

    unsigned char p_encoded[65] = {0x04};
    static const unsigned char p256_p[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    memcpy(p_encoded + 1, p256_p, 32);
    p_encoded[64] = 1;
    neverc_bigint_set(&point.x, &c->gx);
    neverc_bigint_set(&point.y, &c->gy);
    ASSERT_INT_EQ(neverc_elliptic_unmarshal(
                      c, &point, p_encoded, sizeof(p_encoded)),
                  -1);
    ASSERT_TRUE(neverc_bigint_cmp(&point.x, &c->gx) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&point.y, &c->gy) == 0);

    unsigned char one_one[65] = {0x04};
    one_one[32] = 1;
    one_one[64] = 1;
    ASSERT_INT_EQ(neverc_elliptic_unmarshal(
                      c, &point, one_one, sizeof(one_one)),
                  -1);
    ASSERT_TRUE(neverc_bigint_cmp(&point.x, &c->gx) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&point.y, &c->gy) == 0);

    neverc_elliptic_point_free(&point);
}

static void test_x_zero_affine_point(void) {
    printf("[x_zero_affine_point]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t pt, decoded, scaled, neg, sum;
    neverc_elliptic_point_init(&pt);
    neverc_elliptic_point_init(&decoded);
    neverc_elliptic_point_init(&scaled);
    neverc_elliptic_point_init(&neg);
    neverc_elliptic_point_init(&sum);

    neverc_bigint_set_int64(&pt.x, 0);
    ASSERT_INT_EQ(neverc_bigint_set_string(
                      &pt.y,
                      "66485c780e2f83d72433bd5d84a06bb6541c2af31dae871728bf"
                      "856a174f93f4",
                      16),
                  0);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &pt));

    unsigned char encoded[65];
    size_t length = 0;
    ASSERT_INT_EQ(neverc_elliptic_marshal(
                      c, &pt, encoded, sizeof(encoded), &length),
                  0);
    ASSERT_INT_EQ((int)length, 65);
    ASSERT_INT_EQ(encoded[0], 0x04);
    int x_zero = 1;
    for (int i = 1; i < 33; i++)
        if (encoded[i]) x_zero = 0;
    ASSERT_TRUE(x_zero);

    ASSERT_INT_EQ(neverc_elliptic_unmarshal(c, &decoded, encoded, length), 0);
    ASSERT_TRUE(neverc_bigint_is_zero(&decoded.x));
    ASSERT_TRUE(neverc_bigint_cmp(&decoded.y, &pt.y) == 0);

    neverc_bigint_t one;
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&one, 1);
    neverc_elliptic_scalar_mult(c, &scaled, &pt, &one);
    ASSERT_TRUE(neverc_bigint_is_zero(&scaled.x));
    ASSERT_TRUE(neverc_bigint_cmp(&scaled.y, &pt.y) == 0);

    neverc_bigint_set_int64(&neg.x, 0);
    neverc_bigint_sub(&neg.y, &c->p, &pt.y);
    ASSERT_TRUE(neverc_elliptic_is_on_curve(c, &neg));
    neverc_elliptic_add(c, &sum, &pt, &neg);
    ASSERT_TRUE(neverc_bigint_is_zero(&sum.x));
    ASSERT_TRUE(neverc_bigint_is_zero(&sum.y));

    neverc_bigint_free(&one);
    neverc_elliptic_point_free(&pt);
    neverc_elliptic_point_free(&decoded);
    neverc_elliptic_point_free(&scaled);
    neverc_elliptic_point_free(&neg);
    neverc_elliptic_point_free(&sum);
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

static void test_unreduced_coordinates(void) {
    printf("[unreduced_coordinates]\n");
    const neverc_elliptic_curve_t *c = neverc_elliptic_p256();
    neverc_elliptic_point_t g, g_unred, doubled, sum, scaled;
    neverc_elliptic_point_init(&g);
    neverc_elliptic_point_init(&g_unred);
    neverc_elliptic_point_init(&doubled);
    neverc_elliptic_point_init(&sum);
    neverc_elliptic_point_init(&scaled);
    neverc_bigint_set(&g.x, &c->gx);
    neverc_bigint_set(&g.y, &c->gy);
    neverc_elliptic_double(c, &doubled, &g);

    neverc_bigint_add(&g_unred.x, &c->gx, &c->p);
    neverc_bigint_set(&g_unred.y, &c->gy);
    neverc_elliptic_add(c, &sum, &g_unred, &g);
    ASSERT_TRUE(neverc_bigint_cmp(&sum.x, &doubled.x) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&sum.y, &doubled.y) == 0);

    neverc_bigint_set(&g_unred.x, &c->gx);
    neverc_bigint_add(&g_unred.y, &c->gy, &c->p);
    neverc_elliptic_double(c, &sum, &g_unred);
    ASSERT_TRUE(neverc_bigint_cmp(&sum.x, &doubled.x) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&sum.y, &doubled.y) == 0);

    neverc_bigint_t one;
    neverc_bigint_init(&one);
    neverc_bigint_set_int64(&one, 1);
    neverc_bigint_add(&g_unred.x, &c->gx, &c->p);
    neverc_bigint_set(&g_unred.y, &c->gy);
    neverc_elliptic_scalar_mult(c, &scaled, &g_unred, &one);
    ASSERT_TRUE(neverc_bigint_cmp(&scaled.x, &c->gx) == 0);
    ASSERT_TRUE(neverc_bigint_cmp(&scaled.y, &c->gy) == 0);

    neverc_bigint_free(&one);
    neverc_elliptic_point_free(&g);
    neverc_elliptic_point_free(&g_unred);
    neverc_elliptic_point_free(&doubled);
    neverc_elliptic_point_free(&sum);
    neverc_elliptic_point_free(&scaled);
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
    test_concurrent_first_use();
    test_p256_params();
    test_generator_on_curve();
    test_double();
    test_add();
    test_scalar_mult();
    test_marshal_unmarshal();
    test_invalid_affine_points();
    test_x_zero_affine_point();
    test_identity();
    test_unreduced_coordinates();
    test_p384();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

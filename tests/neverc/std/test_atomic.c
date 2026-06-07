#include "neverc/std/sync/atomic.h"
#include <stdio.h>
#include <pthread.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    long long _v = (long long)(expr); long long _e = (long long)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %lld, expected %lld (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_load_store_int32(void) {
    printf("[load_store_int32]\n");
    volatile int32_t v = 0;
    neverc_atomic_store_int32(&v, 42);
    ASSERT_INT_EQ(neverc_atomic_load_int32(&v), 42);

    neverc_atomic_store_int32(&v, -100);
    ASSERT_INT_EQ(neverc_atomic_load_int32(&v), -100);
}

static void test_load_store_int64(void) {
    printf("[load_store_int64]\n");
    volatile int64_t v = 0;
    neverc_atomic_store_int64(&v, 1234567890123LL);
    ASSERT_INT_EQ(neverc_atomic_load_int64(&v), 1234567890123LL);
}

static void test_add(void) {
    printf("[add]\n");
    volatile int32_t v32 = 10;
    int32_t result = neverc_atomic_add_int32(&v32, 5);
    ASSERT_INT_EQ(result, 15);
    ASSERT_INT_EQ(neverc_atomic_load_int32(&v32), 15);

    result = neverc_atomic_add_int32(&v32, -20);
    ASSERT_INT_EQ(result, -5);

    volatile uint64_t v64 = 100;
    uint64_t r64 = neverc_atomic_add_uint64(&v64, 50);
    ASSERT_INT_EQ(r64, 150);
}

static void test_swap(void) {
    printf("[swap]\n");
    volatile int32_t v = 42;
    int32_t old = neverc_atomic_swap_int32(&v, 100);
    ASSERT_INT_EQ(old, 42);
    ASSERT_INT_EQ(neverc_atomic_load_int32(&v), 100);

    volatile int64_t v64 = 999;
    int64_t old64 = neverc_atomic_swap_int64(&v64, 0);
    ASSERT_INT_EQ(old64, 999);
    ASSERT_INT_EQ(neverc_atomic_load_int64(&v64), 0);
}

static void test_cas(void) {
    printf("[cas]\n");
    volatile int32_t v = 10;
    ASSERT_TRUE(neverc_atomic_cas_int32(&v, 10, 20));
    ASSERT_INT_EQ(neverc_atomic_load_int32(&v), 20);

    ASSERT_TRUE(!neverc_atomic_cas_int32(&v, 10, 30));
    ASSERT_INT_EQ(neverc_atomic_load_int32(&v), 20);
}

static void test_pointer(void) {
    printf("[pointer]\n");
    int x = 1, y = 2;
    void *volatile p = &x;
    void *loaded = neverc_atomic_load_pointer(&p);
    ASSERT_TRUE(loaded == &x);

    neverc_atomic_store_pointer(&p, &y);
    ASSERT_TRUE(neverc_atomic_load_pointer(&p) == &y);

    void *old = neverc_atomic_swap_pointer(&p, &x);
    ASSERT_TRUE(old == &y);
    ASSERT_TRUE(neverc_atomic_load_pointer(&p) == &x);

    ASSERT_TRUE(neverc_atomic_cas_pointer(&p, &x, &y));
    ASSERT_TRUE(neverc_atomic_load_pointer(&p) == &y);
}

static volatile int32_t shared_counter = 0;
#define NUM_THREADS 4
#define INCREMENTS 100000

static void *thread_increment(void *arg) {
    (void)arg;
    for (int i = 0; i < INCREMENTS; i++)
        neverc_atomic_add_int32(&shared_counter, 1);
    return NULL;
}

static void test_concurrent_add(void) {
    printf("[concurrent_add]\n");
    shared_counter = 0;
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, thread_increment, NULL);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
    ASSERT_INT_EQ(neverc_atomic_load_int32(&shared_counter), NUM_THREADS * INCREMENTS);
}

int main(void) {
    printf("=== NeverC sync/atomic Tests ===\n");
    test_load_store_int32();
    test_load_store_int64();
    test_add();
    test_swap();
    test_cas();
    test_pointer();
    test_concurrent_add();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

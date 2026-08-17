#include "neverc/std/sync/atomic.h"
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

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

static void test_load_store_uint(void) {
    printf("[load_store_uint]\n");
    volatile uint32_t u32 = 0;
    neverc_atomic_store_uint32(&u32, 7);
    ASSERT_INT_EQ(neverc_atomic_load_uint32(&u32), 7);

    volatile uint64_t u64 = 0;
    neverc_atomic_store_uint64(&u64, 99);
    ASSERT_INT_EQ(neverc_atomic_load_uint64(&u64), 99);

    ASSERT_INT_EQ(neverc_atomic_add_uint32(&u32, 3), 10);
    ASSERT_INT_EQ(neverc_atomic_swap_uint32(&u32, 1), 10);
    ASSERT_INT_EQ(neverc_atomic_load_uint32(&u32), 1);
    ASSERT_TRUE(neverc_atomic_cas_uint32(&u32, 1, 4));
    ASSERT_INT_EQ(neverc_atomic_load_uint32(&u32), 4);

    volatile int64_t i64add = 2;
    ASSERT_INT_EQ(neverc_atomic_add_int64(&i64add, 3), 5);
    volatile uint64_t s64 = 5;
    ASSERT_INT_EQ(neverc_atomic_swap_uint64(&s64, 8), 5);
    ASSERT_TRUE(neverc_atomic_cas_uint64(&s64, 8, 11));
    ASSERT_INT_EQ(neverc_atomic_load_uint64(&s64), 11);

    volatile int64_t i64 = 20;
    ASSERT_TRUE(neverc_atomic_cas_int64(&i64, 20, 30));
    ASSERT_INT_EQ(neverc_atomic_load_int64(&i64), 30);
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
    ASSERT_TRUE(neverc_atomic_load_pointer(&p) == &x);
    ASSERT_TRUE(p == &x);

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

#if defined(_WIN32)
static DWORD WINAPI thread_increment(LPVOID arg) {
    (void)arg;
    for (int i = 0; i < INCREMENTS; i++)
        neverc_atomic_add_int32(&shared_counter, 1);
    return 0;
}
#else
static void *thread_increment(void *arg) {
    (void)arg;
    for (int i = 0; i < INCREMENTS; i++)
        neverc_atomic_add_int32(&shared_counter, 1);
    return NULL;
}
#endif

static void test_concurrent_add(void) {
    printf("[concurrent_add]\n");
    shared_counter = 0;
#if defined(_WIN32)
    HANDLE threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        threads[i] = CreateThread(NULL, 0, thread_increment, NULL, 0, NULL);
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; i++)
        CloseHandle(threads[i]);
#else
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_create(&threads[i], NULL, thread_increment, NULL);
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);
#endif
    ASSERT_INT_EQ(neverc_atomic_load_int32(&shared_counter), NUM_THREADS * INCREMENTS);
}

int main(void) {
    printf("=== NeverC sync/atomic Tests ===\n");
    test_load_store_int32();
    test_load_store_int64();
    test_load_store_uint();
    test_add();
    test_swap();
    test_cas();
    test_pointer();
    test_concurrent_add();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

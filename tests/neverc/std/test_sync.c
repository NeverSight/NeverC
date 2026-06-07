#include "neverc/std/sync.h"
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_mutex_basic(void) {
    printf("[mutex_basic]\n");
    neverc_mutex_t m;
    neverc_mutex_init(&m);
    neverc_mutex_lock(&m);
    neverc_mutex_unlock(&m);
    neverc_mutex_destroy(&m);
    tests_run++; tests_passed++;
}

static void test_mutex_trylock(void) {
    printf("[mutex_trylock]\n");
    neverc_mutex_t m;
    neverc_mutex_init(&m);

    ASSERT_TRUE(neverc_mutex_trylock(&m));
    ASSERT_TRUE(!neverc_mutex_trylock(&m));
    neverc_mutex_unlock(&m);
    ASSERT_TRUE(neverc_mutex_trylock(&m));
    neverc_mutex_unlock(&m);

    neverc_mutex_destroy(&m);
}

static neverc_mutex_t g_mutex;
static int g_counter = 0;
#define M_THREADS 4
#define M_ITERS 50000

#if defined(_WIN32)
static DWORD WINAPI mutex_worker(LPVOID arg) {
    (void)arg;
    for (int i = 0; i < M_ITERS; i++) {
        neverc_mutex_lock(&g_mutex);
        g_counter++;
        neverc_mutex_unlock(&g_mutex);
    }
    return 0;
}
#else
static void *mutex_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < M_ITERS; i++) {
        neverc_mutex_lock(&g_mutex);
        g_counter++;
        neverc_mutex_unlock(&g_mutex);
    }
    return NULL;
}
#endif

static void test_mutex_concurrent(void) {
    printf("[mutex_concurrent]\n");
    neverc_mutex_init(&g_mutex);
    g_counter = 0;

#if defined(_WIN32)
    HANDLE threads[M_THREADS];
    for (int i = 0; i < M_THREADS; i++)
        threads[i] = CreateThread(NULL, 0, mutex_worker, NULL, 0, NULL);
    WaitForMultipleObjects(M_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < M_THREADS; i++)
        CloseHandle(threads[i]);
#else
    pthread_t threads[M_THREADS];
    for (int i = 0; i < M_THREADS; i++)
        pthread_create(&threads[i], NULL, mutex_worker, NULL);
    for (int i = 0; i < M_THREADS; i++)
        pthread_join(threads[i], NULL);
#endif

    ASSERT_INT_EQ(g_counter, M_THREADS * M_ITERS);
    neverc_mutex_destroy(&g_mutex);
}

static void test_rwmutex(void) {
    printf("[rwmutex]\n");
    neverc_rwmutex_t rw;
    neverc_rwmutex_init(&rw);

    neverc_rwmutex_rlock(&rw);
    neverc_rwmutex_rlock(&rw);
    neverc_rwmutex_runlock(&rw);
    neverc_rwmutex_runlock(&rw);

    neverc_rwmutex_lock(&rw);
    neverc_rwmutex_unlock(&rw);

    neverc_rwmutex_destroy(&rw);
    tests_run++; tests_passed++;
}

static neverc_waitgroup_t g_wg;
static volatile int wg_sum = 0;

#if defined(_WIN32)
static DWORD WINAPI wg_worker(LPVOID arg) {
    int val = *(int *)arg;
    __atomic_fetch_add(&wg_sum, val, __ATOMIC_SEQ_CST);
    neverc_waitgroup_done(&g_wg);
    return 0;
}
#else
static void *wg_worker(void *arg) {
    int val = *(int *)arg;
    __atomic_fetch_add(&wg_sum, val, __ATOMIC_SEQ_CST);
    neverc_waitgroup_done(&g_wg);
    return NULL;
}
#endif

static void test_waitgroup(void) {
    printf("[waitgroup]\n");
    neverc_waitgroup_init(&g_wg);
    wg_sum = 0;

    int vals[3] = {10, 20, 30};
    neverc_waitgroup_add(&g_wg, 3);

#if defined(_WIN32)
    HANDLE threads[3];
    for (int i = 0; i < 3; i++)
        threads[i] = CreateThread(NULL, 0, wg_worker, &vals[i], 0, NULL);
    neverc_waitgroup_wait(&g_wg);
    WaitForMultipleObjects(3, threads, TRUE, INFINITE);
    for (int i = 0; i < 3; i++)
        CloseHandle(threads[i]);
#else
    pthread_t threads[3];
    for (int i = 0; i < 3; i++)
        pthread_create(&threads[i], NULL, wg_worker, &vals[i]);
    neverc_waitgroup_wait(&g_wg);
    for (int i = 0; i < 3; i++)
        pthread_join(threads[i], NULL);
#endif

    ASSERT_INT_EQ(wg_sum, 60);
    neverc_waitgroup_destroy(&g_wg);
}

static int once_counter = 0;
static void once_func(void) { once_counter++; }

static neverc_once_t g_once;

#if defined(_WIN32)
static DWORD WINAPI once_worker(LPVOID arg) {
    (void)arg;
    neverc_once_do(&g_once, once_func);
    return 0;
}
#else
static void *once_worker(void *arg) {
    (void)arg;
    neverc_once_do(&g_once, once_func);
    return NULL;
}
#endif

static void test_once(void) {
    printf("[once]\n");
    neverc_once_init(&g_once);
    once_counter = 0;

#if defined(_WIN32)
    HANDLE threads[10];
    for (int i = 0; i < 10; i++)
        threads[i] = CreateThread(NULL, 0, once_worker, NULL, 0, NULL);
    WaitForMultipleObjects(10, threads, TRUE, INFINITE);
    for (int i = 0; i < 10; i++)
        CloseHandle(threads[i]);
#else
    pthread_t threads[10];
    for (int i = 0; i < 10; i++)
        pthread_create(&threads[i], NULL, once_worker, NULL);
    for (int i = 0; i < 10; i++)
        pthread_join(threads[i], NULL);
#endif

    ASSERT_INT_EQ(once_counter, 1);
    neverc_once_destroy(&g_once);
}

static void test_cond(void) {
    printf("[cond]\n");
    neverc_mutex_t m;
    neverc_cond_t c;
    neverc_mutex_init(&m);
    neverc_cond_init(&c, &m);

    neverc_cond_signal(&c);
    neverc_cond_broadcast(&c);

    neverc_cond_destroy(&c);
    neverc_mutex_destroy(&m);
    tests_run++; tests_passed++;
}

int main(void) {
    printf("=== NeverC sync Tests ===\n");
    test_mutex_basic();
    test_mutex_trylock();
    test_mutex_concurrent();
    test_rwmutex();
    test_waitgroup();
    test_once();
    test_cond();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

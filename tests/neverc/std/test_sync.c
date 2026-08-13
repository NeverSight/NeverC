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

static void test_rwmutex_try(void) {
    printf("[rwmutex_try]\n");
    neverc_rwmutex_t rw;
    neverc_rwmutex_init(&rw);

    ASSERT_TRUE(neverc_rwmutex_tryrlock(&rw));
    ASSERT_TRUE(neverc_rwmutex_tryrlock(&rw));
    ASSERT_TRUE(!neverc_rwmutex_trylock(&rw));
    neverc_rwmutex_runlock(&rw);
    neverc_rwmutex_runlock(&rw);

    ASSERT_TRUE(neverc_rwmutex_trylock(&rw));
    ASSERT_TRUE(!neverc_rwmutex_tryrlock(&rw));
    ASSERT_TRUE(!neverc_rwmutex_trylock(&rw));
    neverc_rwmutex_unlock(&rw);

    ASSERT_TRUE(neverc_rwmutex_tryrlock(&rw));
    neverc_rwmutex_runlock(&rw);

    neverc_rwmutex_destroy(&rw);
}

static neverc_waitgroup_t g_wg;
static volatile int wg_sum = 0;

#if defined(_WIN32)
static DWORD WINAPI wg_worker(LPVOID arg) {
    int val = *(int *)arg;
    InterlockedExchangeAdd((volatile long *)&wg_sum, (long)val);
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

static void test_waitgroup_rejects_invalid_counter(void) {
    printf("[waitgroup_invalid_counter]\n");
    neverc_waitgroup_t wg;
    neverc_waitgroup_init(&wg);

    ASSERT_INT_EQ(neverc_waitgroup_done_checked(&wg), -1);
    ASSERT_INT_EQ(wg.counter, 0);
    ASSERT_INT_EQ(neverc_waitgroup_add_checked(&wg, 2), 0);
    ASSERT_INT_EQ(neverc_waitgroup_add_checked(&wg, -3), -1);
    ASSERT_INT_EQ(wg.counter, 2);
    ASSERT_INT_EQ(neverc_waitgroup_done_checked(&wg), 0);
    ASSERT_INT_EQ(neverc_waitgroup_done_checked(&wg), 0);
    ASSERT_INT_EQ(wg.counter, 0);
    ASSERT_INT_EQ(neverc_waitgroup_add_checked(NULL, 1), -1);
    neverc_waitgroup_done(&wg);
    ASSERT_INT_EQ(wg.counter, 0);
    ASSERT_INT_EQ(neverc_waitgroup_add_checked(&wg, INT32_MAX), 0);
    ASSERT_INT_EQ(neverc_waitgroup_add_checked(&wg, 1), -1);
    ASSERT_INT_EQ(wg.counter, INT32_MAX);
    ASSERT_INT_EQ(neverc_waitgroup_add_checked(&wg, -INT32_MAX), 0);

    neverc_waitgroup_wait(&wg);
    neverc_waitgroup_destroy(&wg);
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

#include <stdlib.h>
#include <string.h>

static void *pool_new_func(void) {
    int *p = (int *)malloc(sizeof(int));
    *p = 42;
    return p;
}

static void test_pool_basic(void) {
    printf("[pool_basic]\n");
    neverc_sync_pool_t *pool = neverc_sync_pool_new(pool_new_func);
    ASSERT_TRUE(pool != NULL);

    void *obj = neverc_sync_pool_get(pool);
    ASSERT_TRUE(obj != NULL);
    ASSERT_INT_EQ(*(int *)obj, 42);

    *(int *)obj = 100;
    neverc_sync_pool_put(pool, obj);

    void *obj2 = neverc_sync_pool_get(pool);
    ASSERT_TRUE(obj2 == obj);
    ASSERT_INT_EQ(*(int *)obj2, 100);

    free(obj2);
    neverc_sync_pool_free(pool);
}

static void test_pool_no_new_func(void) {
    printf("[pool_no_new_func]\n");
    neverc_sync_pool_t *pool = neverc_sync_pool_new(NULL);
    void *obj = neverc_sync_pool_get(pool);
    ASSERT_TRUE(obj == NULL);

    int val = 77;
    neverc_sync_pool_put(pool, &val);
    void *got = neverc_sync_pool_get(pool);
    ASSERT_TRUE(got == &val);

    neverc_sync_pool_free(pool);
}

static void test_sync_map_basic(void) {
    printf("[sync_map_basic]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    ASSERT_TRUE(m != NULL);

    int val1 = 10, val2 = 20, val3 = 30;
    neverc_sync_map_store(m, "key1", &val1);
    neverc_sync_map_store(m, "key2", &val2);
    neverc_sync_map_store(m, "key3", &val3);

    int ok = 0;
    void *got = neverc_sync_map_load(m, "key1", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 10);

    got = neverc_sync_map_load(m, "key2", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 20);

    got = neverc_sync_map_load(m, "nonexist", &ok);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(got == NULL);

    neverc_sync_map_free(m);
}

static void test_sync_map_load_invalid_inputs(void) {
    printf("[sync_map_load_invalid_inputs]\n");
    int ok = 1;
    ASSERT_TRUE(neverc_sync_map_load(NULL, "key", &ok) == NULL);
    ASSERT_INT_EQ(ok, 0);

    neverc_sync_map_t *m = neverc_sync_map_new();
    ASSERT_TRUE(m != NULL);
    ok = 1;
    ASSERT_TRUE(neverc_sync_map_load(m, NULL, &ok) == NULL);
    ASSERT_INT_EQ(ok, 0);

    int loaded = 1;
    ASSERT_TRUE(neverc_sync_map_load_and_delete(
                    NULL, "key", &loaded) == NULL);
    ASSERT_INT_EQ(loaded, 0);
    loaded = 1;
    ASSERT_TRUE(neverc_sync_map_load_and_delete(
                    m, NULL, &loaded) == NULL);
    ASSERT_INT_EQ(loaded, 0);

    int value = 1;
    ASSERT_TRUE(!neverc_sync_map_compare_and_swap(
                    NULL, "key", &value, &value));
    ASSERT_TRUE(!neverc_sync_map_compare_and_swap(
                    m, NULL, &value, &value));
    ASSERT_TRUE(!neverc_sync_map_compare_and_delete(
                    NULL, "key", &value));
    ASSERT_TRUE(!neverc_sync_map_compare_and_delete(
                    m, NULL, &value));
    neverc_sync_map_clear(NULL);
    tests_run++;
    tests_passed++;
    neverc_sync_map_range(NULL, NULL, NULL);
    tests_run++;
    tests_passed++;
    neverc_sync_map_store(m, "range-key", &value);
    neverc_sync_map_range(m, NULL, NULL);
    tests_run++;
    tests_passed++;
    neverc_sync_map_free(m);
}

static void test_sync_map_overwrite(void) {
    printf("[sync_map_overwrite]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v1 = 1, v2 = 2;
    neverc_sync_map_store(m, "x", &v1);
    neverc_sync_map_store(m, "x", &v2);

    int ok = 0;
    void *got = neverc_sync_map_load(m, "x", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 2);

    neverc_sync_map_free(m);
}

static void test_sync_map_delete(void) {
    printf("[sync_map_delete]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v = 99;
    neverc_sync_map_store(m, "del_me", &v);

    int ok = 0;
    neverc_sync_map_load(m, "del_me", &ok);
    ASSERT_TRUE(ok);

    neverc_sync_map_delete(m, "del_me");
    neverc_sync_map_load(m, "del_me", &ok);
    ASSERT_TRUE(!ok);

    neverc_sync_map_free(m);
}

static void test_sync_map_load_or_store(void) {
    printf("[sync_map_load_or_store]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v1 = 111, v2 = 222;
    int loaded = 0;

    void *actual = neverc_sync_map_load_or_store(m, "los", &v1, &loaded);
    ASSERT_TRUE(!loaded);
    ASSERT_INT_EQ(*(int *)actual, 111);

    actual = neverc_sync_map_load_or_store(m, "los", &v2, &loaded);
    ASSERT_TRUE(loaded);
    ASSERT_INT_EQ(*(int *)actual, 111);

    neverc_sync_map_free(m);
}

static void test_sync_map_load_and_delete(void) {
    printf("[sync_map_load_and_delete]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v = 555;
    neverc_sync_map_store(m, "lad", &v);

    int loaded = 0;
    void *got = neverc_sync_map_load_and_delete(m, "lad", &loaded);
    ASSERT_TRUE(loaded);
    ASSERT_INT_EQ(*(int *)got, 555);

    int ok = 0;
    neverc_sync_map_load(m, "lad", &ok);
    ASSERT_TRUE(!ok);

    neverc_sync_map_free(m);
}

static void test_sync_map_clear(void) {
    printf("[sync_map_clear]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v = 1;
    neverc_sync_map_store(m, "a", &v);
    neverc_sync_map_store(m, "b", &v);
    neverc_sync_map_store(m, "c", &v);

    neverc_sync_map_clear(m);

    int ok = 0;
    neverc_sync_map_load(m, "a", &ok);
    ASSERT_TRUE(!ok);
    neverc_sync_map_load(m, "b", &ok);
    ASSERT_TRUE(!ok);

    neverc_sync_map_free(m);
}

static int range_count;
static int range_cb(const char *key, void *value, void *user) {
    (void)key; (void)value; (void)user;
    range_count++;
    return 1;
}

typedef struct {
    neverc_sync_map_t *map;
    int *value;
} range_mutation_t;

typedef struct {
    neverc_sync_map_t *map;
    int count;
    int keys_remained_valid;
} range_delete_t;

static int range_store_cb(const char *key, void *value, void *user) {
    (void)key;
    (void)value;
    range_mutation_t *mutation = (range_mutation_t *)user;
    neverc_sync_map_store(mutation->map, "added-during-range",
                          mutation->value);
    return 0;
}

static int range_delete_cb(const char *key, void *value, void *user) {
    (void)value;
    range_delete_t *deletion = (range_delete_t *)user;
    neverc_sync_map_delete(deletion->map, key);
    if (key[0] == '\0')
        deletion->keys_remained_valid = 0;
    deletion->count++;
    return 1;
}

static void test_sync_map_range(void) {
    printf("[sync_map_range]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v = 1;
    neverc_sync_map_store(m, "r1", &v);
    neverc_sync_map_store(m, "r2", &v);
    neverc_sync_map_store(m, "r3", &v);

    range_count = 0;
    neverc_sync_map_range(m, range_cb, NULL);
    ASSERT_INT_EQ(range_count, 3);

    range_mutation_t mutation = {m, &v};
    neverc_sync_map_range(m, range_store_cb, &mutation);
    int ok = 0;
    ASSERT_TRUE(neverc_sync_map_load(
                    m, "added-during-range", &ok) == &v);
    ASSERT_TRUE(ok);

    range_delete_t deletion = {m, 0, 1};
    neverc_sync_map_range(m, range_delete_cb, &deletion);
    ASSERT_INT_EQ(deletion.count, 4);
    ASSERT_TRUE(deletion.keys_remained_valid);
    range_count = 0;
    neverc_sync_map_range(m, range_cb, NULL);
    ASSERT_INT_EQ(range_count, 0);

    neverc_sync_map_free(m);
}

static void test_sync_map_grow(void) {
    printf("[sync_map_grow]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    char key[32];
    int vals[100];
    for (int i = 0; i < 100; i++) {
        vals[i] = i;
        snprintf(key, sizeof(key), "key_%d", i);
        neverc_sync_map_store(m, key, &vals[i]);
    }
    int ok = 0;
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        void *got = neverc_sync_map_load(m, key, &ok);
        ASSERT_TRUE(ok);
        ASSERT_INT_EQ(*(int *)got, i);
    }
    neverc_sync_map_free(m);
}

static void test_sync_map_delete_then_lookup(void) {
    printf("[sync_map_delete_then_lookup]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();

    int v1 = 1, v2 = 2, v3 = 3, v4 = 4, v5 = 5;
    neverc_sync_map_store(m, "alpha", &v1);
    neverc_sync_map_store(m, "beta",  &v2);
    neverc_sync_map_store(m, "gamma", &v3);
    neverc_sync_map_store(m, "delta", &v4);
    neverc_sync_map_store(m, "epsilon", &v5);

    neverc_sync_map_delete(m, "beta");
    neverc_sync_map_delete(m, "delta");

    int ok = 0;
    void *got = neverc_sync_map_load(m, "alpha", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 1);

    got = neverc_sync_map_load(m, "gamma", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 3);

    got = neverc_sync_map_load(m, "epsilon", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 5);

    neverc_sync_map_load(m, "beta", &ok);
    ASSERT_TRUE(!ok);
    neverc_sync_map_load(m, "delta", &ok);
    ASSERT_TRUE(!ok);

    int v6 = 66;
    neverc_sync_map_store(m, "beta", &v6);
    got = neverc_sync_map_load(m, "beta", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 66);

    neverc_sync_map_free(m);
}

static void test_sync_map_stress_delete(void) {
    printf("[sync_map_stress_delete]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    char key[32];
    int vals[200];

    for (int i = 0; i < 200; i++) {
        vals[i] = i * 10;
        snprintf(key, sizeof(key), "stress_%d", i);
        neverc_sync_map_store(m, key, &vals[i]);
    }

    for (int i = 0; i < 200; i += 2) {
        snprintf(key, sizeof(key), "stress_%d", i);
        neverc_sync_map_delete(m, key);
    }

    int ok = 0;
    for (int i = 0; i < 200; i++) {
        snprintf(key, sizeof(key), "stress_%d", i);
        void *got = neverc_sync_map_load(m, key, &ok);
        if (i % 2 == 0) {
            ASSERT_TRUE(!ok);
        } else {
            ASSERT_TRUE(ok);
            ASSERT_INT_EQ(*(int *)got, i * 10);
        }
    }

    for (int i = 0; i < 200; i += 2) {
        vals[i] = i * 100;
        snprintf(key, sizeof(key), "stress_%d", i);
        neverc_sync_map_store(m, key, &vals[i]);
    }

    for (int i = 0; i < 200; i++) {
        snprintf(key, sizeof(key), "stress_%d", i);
        void *got = neverc_sync_map_load(m, key, &ok);
        ASSERT_TRUE(ok);
        if (i % 2 == 0) {
            ASSERT_INT_EQ(*(int *)got, i * 100);
        } else {
            ASSERT_INT_EQ(*(int *)got, i * 10);
        }
    }

    neverc_sync_map_free(m);
}

static void test_sync_map_swap(void) {
    printf("[sync_map_swap]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v1 = 10, v2 = 20, v3 = 30;
    int loaded = 0;

    void *prev = neverc_sync_map_swap(m, "sw", &v1, &loaded);
    ASSERT_TRUE(!loaded);
    ASSERT_TRUE(prev == NULL);

    prev = neverc_sync_map_swap(m, "sw", &v2, &loaded);
    ASSERT_TRUE(loaded);
    ASSERT_TRUE(prev == &v1);

    prev = neverc_sync_map_swap(m, "sw", &v3, &loaded);
    ASSERT_TRUE(loaded);
    ASSERT_TRUE(prev == &v2);

    int ok = 0;
    void *got = neverc_sync_map_load(m, "sw", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 30);

    neverc_sync_map_free(m);
}

static void test_sync_map_compare_and_swap(void) {
    printf("[sync_map_compare_and_swap]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v1 = 100, v2 = 200, v3 = 300;
    neverc_sync_map_store(m, "cas", &v1);

    ASSERT_TRUE(!neverc_sync_map_compare_and_swap(m, "cas", &v2, &v3));

    int ok = 0;
    void *got = neverc_sync_map_load(m, "cas", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 100);

    ASSERT_TRUE(neverc_sync_map_compare_and_swap(m, "cas", &v1, &v2));
    got = neverc_sync_map_load(m, "cas", &ok);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(*(int *)got, 200);

    ASSERT_TRUE(!neverc_sync_map_compare_and_swap(m, "nonexist", &v1, &v2));

    neverc_sync_map_free(m);
}

static void test_sync_map_compare_and_delete(void) {
    printf("[sync_map_compare_and_delete]\n");
    neverc_sync_map_t *m = neverc_sync_map_new();
    int v1 = 42, v2 = 99;
    neverc_sync_map_store(m, "cad", &v1);

    ASSERT_TRUE(!neverc_sync_map_compare_and_delete(m, "cad", &v2));

    int ok = 0;
    neverc_sync_map_load(m, "cad", &ok);
    ASSERT_TRUE(ok);

    ASSERT_TRUE(neverc_sync_map_compare_and_delete(m, "cad", &v1));
    neverc_sync_map_load(m, "cad", &ok);
    ASSERT_TRUE(!ok);

    ASSERT_TRUE(!neverc_sync_map_compare_and_delete(m, "nonexist", &v1));

    neverc_sync_map_free(m);
}

int main(void) {
    printf("=== NeverC sync Tests ===\n");
    test_mutex_basic();
    test_mutex_trylock();
    test_mutex_concurrent();
    test_rwmutex();
    test_rwmutex_try();
    test_waitgroup();
    test_waitgroup_rejects_invalid_counter();
    test_once();
    test_cond();
    test_pool_basic();
    test_pool_no_new_func();
    test_sync_map_basic();
    test_sync_map_load_invalid_inputs();
    test_sync_map_overwrite();
    test_sync_map_delete();
    test_sync_map_load_or_store();
    test_sync_map_load_and_delete();
    test_sync_map_clear();
    test_sync_map_range();
    test_sync_map_grow();
    test_sync_map_delete_then_lookup();
    test_sync_map_stress_delete();
    test_sync_map_swap();
    test_sync_map_compare_and_swap();
    test_sync_map_compare_and_delete();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}

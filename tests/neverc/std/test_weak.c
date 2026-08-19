#include "neverc/std/weak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define ASSERT_TRUE(expr) do { tests_run++; if(expr)tests_passed++; else{tests_failed++; printf("  FAIL: %s (line %d)\n",#expr,__LINE__);}} while(0)
#define ASSERT_INT_EQ(expr, expected) do { long long _v=(long long)(expr); long long _e=(long long)(expected); tests_run++; if(_v==_e)tests_passed++; else{tests_failed++; printf("  FAIL: %s = %lld, expected %lld (line %d)\n",#expr,_v,_e,__LINE__);}} while(0)

static void test_strong_basic(void) {
    printf("[strong_basic]\n");
    int data = 42;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    ASSERT_TRUE(s.ptr != NULL);
    ASSERT_INT_EQ(*(int *)s.ptr, 42);
    ASSERT_INT_EQ(neverc_weak_strong_count(s), 1);
    neverc_weak_strong_release(&s);
    ASSERT_TRUE(s.ptr == NULL);
}

static void test_strong_retain(void) {
    printf("[strong_retain]\n");
    int data = 99;
    neverc_weak_strong_t s1 = neverc_weak_new(&data, sizeof(int));
    neverc_weak_strong_t s2 = neverc_weak_strong_retain(s1);
    ASSERT_INT_EQ(neverc_weak_strong_count(s1), 2);
    ASSERT_INT_EQ(*(int *)s2.ptr, 99);
    neverc_weak_strong_release(&s1);
    ASSERT_INT_EQ(*(int *)s2.ptr, 99);
    ASSERT_INT_EQ(neverc_weak_strong_count(s2), 1);
    neverc_weak_strong_release(&s2);
}

static void test_weak_ref_alive(void) {
    printf("[weak_ref_alive]\n");
    int data = 77;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_ref_t *w = neverc_weak_make(s);
    ASSERT_TRUE(w != NULL);

    void *val = neverc_weak_value(w);
    ASSERT_TRUE(val != NULL);
    ASSERT_INT_EQ(*(int *)val, 77);

    neverc_weak_strong_release(&s);
    neverc_weak_ref_release(w);
}

static void test_weak_ref_expired(void) {
    printf("[weak_ref_expired]\n");
    int data = 55;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_ref_t *w = neverc_weak_make(s);

    neverc_weak_strong_release(&s);

    void *val = neverc_weak_value(w);
    ASSERT_TRUE(val == NULL);

    neverc_weak_strong_t upgraded = neverc_weak_upgrade(w);
    ASSERT_TRUE(upgraded.ptr == NULL);

    neverc_weak_ref_release(w);
}

static void test_upgrade(void) {
    printf("[upgrade]\n");
    int data = 123;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_ref_t *w = neverc_weak_make(s);

    neverc_weak_strong_t s2 = neverc_weak_upgrade(w);
    ASSERT_TRUE(s2.ptr != NULL);
    ASSERT_INT_EQ(*(int *)s2.ptr, 123);
    ASSERT_INT_EQ(neverc_weak_strong_count(s), 2);

    neverc_weak_strong_release(&s);
    ASSERT_INT_EQ(*(int *)s2.ptr, 123);
    ASSERT_INT_EQ(neverc_weak_strong_count(s2), 1);

    neverc_weak_strong_release(&s2);
    ASSERT_TRUE(neverc_weak_value(w) == NULL);
    neverc_weak_ref_release(w);
}

static void test_ref_equal(void) {
    printf("[ref_equal]\n");
    int data = 10;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_ref_t *w1 = neverc_weak_make(s);
    neverc_weak_ref_t *w2 = neverc_weak_make(s);

    ASSERT_TRUE(neverc_weak_ref_equal(w1, w2));

    int data2 = 20;
    neverc_weak_strong_t s2 = neverc_weak_new(&data2, sizeof(int));
    neverc_weak_ref_t *w3 = neverc_weak_make(s2);

    ASSERT_TRUE(!neverc_weak_ref_equal(w1, w3));

    neverc_weak_ref_release(w1);
    neverc_weak_ref_release(w2);
    neverc_weak_ref_release(w3);
    neverc_weak_strong_release(&s);
    neverc_weak_strong_release(&s2);
}

static int g_freed = 0;
static void custom_free(void *p) { (void)p; g_freed = 1; }

static void test_custom_free(void) {
    printf("[custom_free]\n");
    g_freed = 0;
    int data = 5;
    neverc_weak_strong_t s = neverc_weak_new_with_free(&data, custom_free);
    ASSERT_TRUE(s.ptr != NULL);
    ASSERT_INT_EQ(g_freed, 0);
    neverc_weak_strong_release(&s);
    ASSERT_INT_EQ(g_freed, 1);
}

static void test_make_requires_live_strong(void) {
    printf("[make_requires_live_strong]\n");
    int data = 7;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_ref_t *keep = neverc_weak_make(s);
    ASSERT_TRUE(keep != NULL);
    neverc_weak_strong_t stale = s;
    neverc_weak_strong_release(&s);
    ASSERT_TRUE(neverc_weak_value(keep) == NULL);
    ASSERT_TRUE(neverc_weak_make(stale) == NULL);
    neverc_weak_ref_release(keep);
}

static void test_stale_strong_without_weak(void) {
    printf("[stale_strong_without_weak]\n");
    int data = 11;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_strong_t stale = s;
    neverc_weak_strong_release(&s);
    neverc_weak_strong_t retained = neverc_weak_strong_retain(stale);
    ASSERT_TRUE(retained.ptr == NULL);
    ASSERT_TRUE(retained._ctrl == NULL);
    ASSERT_TRUE(neverc_weak_make(stale) == NULL);
    ASSERT_INT_EQ(neverc_weak_strong_count(stale), 0);
    neverc_weak_strong_release(&stale);
}

static void test_multiple_weak_refs(void) {
    printf("[multiple_weak_refs]\n");
    int data = 42;
    neverc_weak_strong_t s = neverc_weak_new(&data, sizeof(int));
    neverc_weak_ref_t *refs[10];
    for (int i = 0; i < 10; i++)
        refs[i] = neverc_weak_make(s);

    for (int i = 0; i < 10; i++) {
        void *val = neverc_weak_value(refs[i]);
        ASSERT_TRUE(val != NULL);
        ASSERT_INT_EQ(*(int *)val, 42);
    }

    neverc_weak_strong_release(&s);

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(neverc_weak_value(refs[i]) == NULL);
        neverc_weak_ref_release(refs[i]);
    }
}

static int g_payload_frees;

static void track_payload_free(void *p) {
    __atomic_fetch_add(&g_payload_frees, 1, __ATOMIC_SEQ_CST);
    free(p);
}

static int payload_frees(void) {
    return __atomic_load_n(&g_payload_frees, __ATOMIC_SEQ_CST);
}

static void *owned_int(int v) {
    int *p = (int *)malloc(sizeof(*p));
    if (p) *p = v;
    return p;
}

/* Bitwise copy of a released strong must not drop a recycled control block's
 * new payload. Sequential case: epoch mismatch after retire. */
static void test_stale_release_after_recycle(void) {
    printf("[stale_release_after_recycle]\n");
    __atomic_store_n(&g_payload_frees, 0, __ATOMIC_SEQ_CST);
    neverc_weak_strong_t s = neverc_weak_new_with_free(owned_int(1), track_payload_free);
    ASSERT_TRUE(s.ptr != NULL);
    neverc_weak_strong_t stale = s;
    neverc_weak_strong_release(&s);
    ASSERT_INT_EQ(payload_frees(), 1);

    neverc_weak_strong_t n = neverc_weak_new_with_free(owned_int(2), track_payload_free);
    ASSERT_TRUE(n.ptr != NULL);
    ASSERT_INT_EQ(*(int *)n.ptr, 2);
    neverc_weak_strong_release(&stale);
    ASSERT_INT_EQ(payload_frees(), 1);
    ASSERT_TRUE(n.ptr != NULL);
    ASSERT_INT_EQ(*(int *)n.ptr, 2);
    ASSERT_INT_EQ(neverc_weak_strong_count(n), 1);
    neverc_weak_strong_release(&n);
    ASSERT_INT_EQ(payload_frees(), 2);
}

#if defined(_WIN32)
static DWORD WINAPI release_stale_thread(LPVOID arg) {
    neverc_weak_strong_release((neverc_weak_strong_t *)arg);
    return 0;
}
#else
static void *release_stale_thread(void *arg) {
    neverc_weak_strong_release((neverc_weak_strong_t *)arg);
    return NULL;
}
#endif

/* Same contract under overlap: stale release vs last live release + recycle. */
static void test_stale_release_concurrent_recycle(void) {
    printf("[stale_release_concurrent_recycle]\n");
    enum { N = 256 };
    int ok = 1;
    for (int i = 0; i < N && ok; i++) {
        __atomic_store_n(&g_payload_frees, 0, __ATOMIC_SEQ_CST);
        neverc_weak_strong_t live =
            neverc_weak_new_with_free(owned_int(1), track_payload_free);
        if (!live.ptr) { ok = 0; break; }
        neverc_weak_strong_t stale = live;
#if defined(_WIN32)
        HANDLE th = CreateThread(NULL, 0, release_stale_thread, &stale, 0, NULL);
        if (!th) { neverc_weak_strong_release(&live); ok = 0; break; }
#else
        pthread_t th;
        if (pthread_create(&th, NULL, release_stale_thread, &stale) != 0) {
            neverc_weak_strong_release(&live);
            ok = 0;
            break;
        }
#endif
        neverc_weak_strong_release(&live);
        neverc_weak_strong_t recycled =
            neverc_weak_new_with_free(owned_int(2), track_payload_free);
#if defined(_WIN32)
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
#else
        pthread_join(th, NULL);
#endif
        if (!recycled.ptr || *(int *)recycled.ptr != 2 ||
            neverc_weak_strong_count(recycled) != 1 || payload_frees() != 1)
            ok = 0;
        neverc_weak_strong_release(&recycled);
        if (payload_frees() != 2)
            ok = 0;
    }
    ASSERT_TRUE(ok);
}

int main(void) {
    test_strong_basic();
    test_strong_retain();
    test_weak_ref_alive();
    test_weak_ref_expired();
    test_upgrade();
    test_ref_equal();
    test_custom_free();
    test_make_requires_live_strong();
    test_stale_strong_without_weak();
    test_multiple_weak_refs();
    test_stale_release_after_recycle();
    test_stale_release_concurrent_recycle();
    printf("\nweak: %d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}

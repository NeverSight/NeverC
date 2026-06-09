#include "neverc/std/weak.h"
#include <stdio.h>
#include <string.h>

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

int main(void) {
    test_strong_basic();
    test_strong_retain();
    test_weak_ref_alive();
    test_weak_ref_expired();
    test_upgrade();
    test_ref_equal();
    test_custom_free();
    test_multiple_weak_refs();
    printf("\nweak: %d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}

#include "neverc/maps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define ASSERT_INT_EQ(expr, expected) do { int _v=(int)(expr); int _e=(int)(expected); tests_run++; if(_v==_e)tests_passed++; else{tests_failed++; printf("  FAIL: %s=%d, expected %d (line %d)\n",#expr,_v,_e,__LINE__);}} while(0)
#define ASSERT_TRUE(expr) do { tests_run++; if(expr)tests_passed++; else{tests_failed++; printf("  FAIL: %s (line %d)\n",#expr,__LINE__);}} while(0)

static void test_basic(void) {
    printf("[basic]\n");
    neverc_map_t *m = neverc_map_new();
    ASSERT_TRUE(m != NULL);
    ASSERT_INT_EQ((int)neverc_map_len(m), 0);

    int v1 = 100, v2 = 200;
    neverc_map_set(m, "hello", &v1);
    neverc_map_set(m, "world", &v2);
    ASSERT_INT_EQ((int)neverc_map_len(m), 2);

    ASSERT_TRUE(neverc_map_has(m, "hello"));
    ASSERT_TRUE(!neverc_map_has(m, "foo"));

    int *got = (int *)neverc_map_get(m, "hello");
    ASSERT_TRUE(got != NULL);
    ASSERT_INT_EQ(*got, 100);

    got = (int *)neverc_map_get(m, "world");
    ASSERT_TRUE(got != NULL);
    ASSERT_INT_EQ(*got, 200);

    ASSERT_TRUE(neverc_map_get(m, "missing") == NULL);

    neverc_map_free(m);
}

static void test_overwrite(void) {
    printf("[overwrite]\n");
    neverc_map_t *m = neverc_map_new();
    int v1 = 10, v2 = 20;
    neverc_map_set(m, "key", &v1);
    neverc_map_set(m, "key", &v2);
    ASSERT_INT_EQ((int)neverc_map_len(m), 1);
    int *got = (int *)neverc_map_get(m, "key");
    ASSERT_INT_EQ(*got, 20);
    neverc_map_free(m);
}

static void test_delete(void) {
    printf("[delete]\n");
    neverc_map_t *m = neverc_map_new();
    int v = 42;
    neverc_map_set(m, "a", &v);
    neverc_map_set(m, "b", &v);
    neverc_map_set(m, "c", &v);
    ASSERT_INT_EQ((int)neverc_map_len(m), 3);

    neverc_map_delete(m, "b");
    ASSERT_INT_EQ((int)neverc_map_len(m), 2);
    ASSERT_TRUE(!neverc_map_has(m, "b"));
    ASSERT_TRUE(neverc_map_has(m, "a"));
    ASSERT_TRUE(neverc_map_has(m, "c"));

    neverc_map_free(m);
}

static void test_clear(void) {
    printf("[clear]\n");
    neverc_map_t *m = neverc_map_new();
    int v = 1;
    for (int i = 0; i < 10; i++) {
        char key[16]; snprintf(key, sizeof(key), "k%d", i);
        neverc_map_set(m, key, &v);
    }
    ASSERT_INT_EQ((int)neverc_map_len(m), 10);
    neverc_map_clear(m);
    ASSERT_INT_EQ((int)neverc_map_len(m), 0);
    neverc_map_free(m);
}

static void test_keys_values(void) {
    printf("[keys_values]\n");
    neverc_map_t *m = neverc_map_new();
    int v1 = 1, v2 = 2;
    neverc_map_set(m, "x", &v1);
    neverc_map_set(m, "y", &v2);

    size_t count;
    char **keys = neverc_map_keys(m, &count);
    ASSERT_INT_EQ((int)count, 2);
    free(keys);

    void **vals = neverc_map_values(m, &count);
    ASSERT_INT_EQ((int)count, 2);
    free(vals);

    neverc_map_free(m);
}

static void test_clone(void) {
    printf("[clone]\n");
    neverc_map_t *m = neverc_map_new();
    int v1 = 10, v2 = 20;
    neverc_map_set(m, "a", &v1);
    neverc_map_set(m, "b", &v2);

    neverc_map_t *c = neverc_map_clone(m);
    ASSERT_INT_EQ((int)neverc_map_len(c), 2);
    ASSERT_TRUE(neverc_map_has(c, "a"));
    ASSERT_TRUE(neverc_map_has(c, "b"));
    ASSERT_TRUE(neverc_map_equal(m, c));

    neverc_map_free(c);
    neverc_map_free(m);
}

static void test_many_entries(void) {
    printf("[many_entries]\n");
    neverc_map_t *m = neverc_map_new();
    int vals[100];
    for (int i = 0; i < 100; i++) {
        vals[i] = i * 10;
        char key[32]; snprintf(key, sizeof(key), "entry_%d", i);
        neverc_map_set(m, key, &vals[i]);
    }
    ASSERT_INT_EQ((int)neverc_map_len(m), 100);

    for (int i = 0; i < 100; i++) {
        char key[32]; snprintf(key, sizeof(key), "entry_%d", i);
        int *got = (int *)neverc_map_get(m, key);
        ASSERT_TRUE(got != NULL);
        ASSERT_INT_EQ(*got, i * 10);
    }

    neverc_map_free(m);
}

int main(void) {
    printf("=== NeverC maps Tests ===\n");
    test_basic();
    test_overwrite();
    test_delete();
    test_clear();
    test_keys_values();
    test_clone();
    test_many_entries();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}

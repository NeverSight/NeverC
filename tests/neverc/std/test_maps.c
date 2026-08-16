#include "neverc/std/maps.h"
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

static void test_copy(void) {
    printf("[copy]\n");
    static int v1 = 1, v2 = 2, v3 = 3, v4 = 4;
    neverc_map_t *dst = neverc_map_new();
    neverc_map_t *src = neverc_map_new();

    neverc_map_set(dst, "a", &v1);
    neverc_map_set(dst, "b", &v2);
    neverc_map_set(src, "b", &v3);
    neverc_map_set(src, "c", &v4);

    neverc_map_copy(dst, src);

    ASSERT_INT_EQ((int)neverc_map_len(dst), 3);
    ASSERT_INT_EQ(*(int *)neverc_map_get(dst, "a"), 1);
    ASSERT_INT_EQ(*(int *)neverc_map_get(dst, "b"), 3);
    ASSERT_INT_EQ(*(int *)neverc_map_get(dst, "c"), 4);

    neverc_map_free(dst);
    neverc_map_free(src);
}

static int delete_even_filter(const char *key, void *value) {
    (void)key;
    return (*(int *)value) % 2 == 0;
}

static neverc_map_t *g_filter_map;

static int delete_current_but_keep_filter(const char *key, void *value) {
    (void)value;
    neverc_maps_delete(g_filter_map, key);
    return 0;
}

static void test_delete_func_callback_can_delete_current(void) {
    printf("[delete_func_callback_delete_current]\n");
    neverc_map_t *m = neverc_map_new();
    int value = 1;
    neverc_map_set(m, "victim", &value);
    g_filter_map = m;

    neverc_maps_delete_func(m, delete_current_but_keep_filter);

    ASSERT_INT_EQ((int)neverc_map_len(m), 0);
    ASSERT_TRUE(!neverc_map_has(m, "victim"));
    g_filter_map = NULL;
    neverc_map_free(m);
}

static neverc_map_t *g_foreach_map;
static size_t g_foreach_key_length;

static void delete_current_then_read_key(const char *key, void *value,
                                         void *user_data) {
    (void)value;
    (void)user_data;
    neverc_maps_delete(g_foreach_map, key);
    g_foreach_key_length = strlen(key);
}

static void test_foreach_callback_key_survives_mutation(void) {
    printf("[foreach_callback_key_survives_mutation]\n");
    neverc_map_t *m = neverc_map_new();
    int value = 1;
    neverc_map_set(m, "victim", &value);
    g_foreach_map = m;
    g_foreach_key_length = 0;

    neverc_maps_foreach(m, delete_current_then_read_key, NULL);

    ASSERT_INT_EQ((int)g_foreach_key_length, 6);
    ASSERT_INT_EQ((int)neverc_map_len(m), 0);
    g_foreach_map = NULL;
    neverc_map_free(m);
}

static int g_resize_values[100];
static int g_resize_inserted;

static int insert_during_filter(const char *key, void *value) {
    (void)value;
    if (!g_resize_inserted) {
        g_resize_inserted = 1;
        for (int i = 0; i < 100; i++) {
            char inserted[32];
            snprintf(inserted, sizeof(inserted), "inserted_%d", i);
            g_resize_values[i] = i;
            neverc_maps_set(g_filter_map, inserted, &g_resize_values[i]);
        }
    }
    return strcmp(key, "remove") == 0;
}

static void test_delete_func_callback_can_resize(void) {
    printf("[delete_func_callback_resize]\n");
    neverc_map_t *m = neverc_map_new();
    int keep = 1, remove = 2;
    neverc_map_set(m, "keep", &keep);
    neverc_map_set(m, "remove", &remove);
    g_filter_map = m;
    g_resize_inserted = 0;

    neverc_maps_delete_func(m, insert_during_filter);

    ASSERT_INT_EQ((int)neverc_map_len(m), 101);
    ASSERT_TRUE(neverc_map_has(m, "keep"));
    ASSERT_TRUE(!neverc_map_has(m, "remove"));
    ASSERT_TRUE(neverc_map_has(m, "inserted_99"));
    g_filter_map = NULL;
    neverc_map_free(m);
}

static void test_delete_func_probe_chain(void) {
    printf("[delete_func_probe_chain]\n");
    neverc_map_t *m = neverc_map_new();
    int vals[50];
    char key[32];
    for (int i = 0; i < 50; i++) {
        vals[i] = i;
        snprintf(key, sizeof(key), "k%d", i);
        neverc_map_set(m, key, &vals[i]);
    }

    neverc_maps_delete_func(m, delete_even_filter);

    for (int i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        if (i % 2 == 0) {
            ASSERT_TRUE(!neverc_map_has(m, key));
        } else {
            ASSERT_TRUE(neverc_map_has(m, key));
            ASSERT_INT_EQ(*(int *)neverc_map_get(m, key), i);
        }
    }

    neverc_map_free(m);
}

static void test_tombstone_shrink_then_reinsert(void) {
    printf("[tombstone_shrink_then_reinsert]\n");
    neverc_map_t *m = neverc_map_new();
    int vals[80];
    char key[32];
    for (int i = 0; i < 80; i++) {
        vals[i] = i + 1;
        snprintf(key, sizeof(key), "t%d", i);
        ASSERT_INT_EQ(neverc_map_set(m, key, &vals[i]), 0);
    }
    ASSERT_INT_EQ((int)neverc_map_len(m), 80);

    /* Drop most keys so tombstones outnumber live entries and force a
     * shrink/rehash; surviving keys must still resolve. */
    for (int i = 0; i < 70; i++) {
        snprintf(key, sizeof(key), "t%d", i);
        ASSERT_INT_EQ(neverc_map_delete(m, key), 0);
    }
    ASSERT_INT_EQ((int)neverc_map_len(m), 10);
    for (int i = 70; i < 80; i++) {
        snprintf(key, sizeof(key), "t%d", i);
        ASSERT_TRUE(neverc_map_has(m, key));
        ASSERT_INT_EQ(*(int *)neverc_map_get(m, key), i + 1);
    }
    for (int i = 0; i < 70; i++) {
        snprintf(key, sizeof(key), "t%d", i);
        ASSERT_TRUE(!neverc_map_has(m, key));
        ASSERT_INT_EQ(neverc_map_set(m, key, &vals[i]), 0);
    }
    ASSERT_INT_EQ((int)neverc_map_len(m), 80);
    for (int i = 0; i < 80; i++) {
        snprintf(key, sizeof(key), "t%d", i);
        ASSERT_TRUE(neverc_map_has(m, key));
        ASSERT_INT_EQ(*(int *)neverc_map_get(m, key), i + 1);
    }
    neverc_map_free(m);
}

static void test_delete_then_reinsert(void) {
    printf("[delete_then_reinsert]\n");
    neverc_map_t *m = neverc_map_new();
    int v1 = 1, v2 = 2, v3 = 3, v4 = 4, v5 = 5;
    neverc_map_set(m, "alpha", &v1);
    neverc_map_set(m, "beta",  &v2);
    neverc_map_set(m, "gamma", &v3);
    neverc_map_set(m, "delta", &v4);
    neverc_map_set(m, "epsilon", &v5);

    neverc_map_delete(m, "beta");
    neverc_map_delete(m, "delta");

    ASSERT_TRUE(neverc_map_has(m, "alpha"));
    ASSERT_TRUE(!neverc_map_has(m, "beta"));
    ASSERT_TRUE(neverc_map_has(m, "gamma"));
    ASSERT_TRUE(!neverc_map_has(m, "delta"));
    ASSERT_TRUE(neverc_map_has(m, "epsilon"));

    int v6 = 66;
    neverc_map_set(m, "beta", &v6);
    ASSERT_TRUE(neverc_map_has(m, "beta"));
    ASSERT_INT_EQ(*(int *)neverc_map_get(m, "beta"), 66);
    ASSERT_INT_EQ((int)neverc_map_len(m), 4);

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
    test_copy();
    test_delete_func_probe_chain();
    test_delete_func_callback_can_delete_current();
    test_foreach_callback_key_survives_mutation();
    test_delete_func_callback_can_resize();
    test_tombstone_shrink_then_reinsert();
    test_delete_then_reinsert();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}

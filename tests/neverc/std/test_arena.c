#include "neverc/std/arena.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define ASSERT_TRUE(expr) do { tests_run++; if(expr)tests_passed++; else{tests_failed++; printf("  FAIL: %s (line %d)\n",#expr,__LINE__);}} while(0)
#define ASSERT_INT_EQ(expr, expected) do { long long _v=(long long)(expr); long long _e=(long long)(expected); tests_run++; if(_v==_e)tests_passed++; else{tests_failed++; printf("  FAIL: %s = %lld, expected %lld (line %d)\n",#expr,_v,_e,__LINE__);}} while(0)
#define ASSERT_STR_EQ(a, b) do { tests_run++; if(strcmp(a,b)==0)tests_passed++; else{tests_failed++; printf("  FAIL: \"%s\" != \"%s\" (line %d)\n",(a),(b),__LINE__);}} while(0)

static void test_basic_alloc(void) {
    printf("[basic_alloc]\n");
    neverc_arena_t *a = neverc_arena_new();
    ASSERT_TRUE(a != NULL);

    int *p1 = (int *)neverc_arena_alloc(a, sizeof(int));
    ASSERT_TRUE(p1 != NULL);
    *p1 = 42;
    ASSERT_INT_EQ(*p1, 42);

    double *p2 = (double *)neverc_arena_alloc(a, sizeof(double));
    ASSERT_TRUE(p2 != NULL);
    *p2 = 3.14;

    ASSERT_TRUE(neverc_arena_bytes_allocated(a) >= sizeof(int) + sizeof(double));
    neverc_arena_free(a);
}

static void test_calloc(void) {
    printf("[calloc]\n");
    neverc_arena_t *a = neverc_arena_new();
    int *arr = (int *)neverc_arena_calloc(a, 100, sizeof(int));
    ASSERT_TRUE(arr != NULL);
    for (int i = 0; i < 100; i++)
        ASSERT_INT_EQ(arr[i], 0);
    neverc_arena_free(a);
}

static void test_strdup(void) {
    printf("[strdup]\n");
    neverc_arena_t *a = neverc_arena_new();

    char *s1 = neverc_arena_strdup(a, "hello world");
    ASSERT_TRUE(s1 != NULL);
    ASSERT_STR_EQ(s1, "hello world");

    char *s2 = neverc_arena_strndup(a, "hello world", 5);
    ASSERT_TRUE(s2 != NULL);
    ASSERT_STR_EQ(s2, "hello");

    ASSERT_TRUE(neverc_arena_strdup(a, NULL) == NULL);
    neverc_arena_free(a);
}

static void test_memdup(void) {
    printf("[memdup]\n");
    neverc_arena_t *a = neverc_arena_new();
    int src[] = {10, 20, 30, 40, 50};
    int *dst = (int *)neverc_arena_memdup(a, src, sizeof(src));
    ASSERT_TRUE(dst != NULL);
    for (int i = 0; i < 5; i++)
        ASSERT_INT_EQ(dst[i], src[i]);
    neverc_arena_free(a);
}

static void test_large_alloc(void) {
    printf("[large_alloc]\n");
    neverc_arena_t *a = neverc_arena_new();
    size_t big = 256 * 1024;
    char *p = (char *)neverc_arena_alloc(a, big);
    ASSERT_TRUE(p != NULL);
    memset(p, 0xAB, big);
    ASSERT_INT_EQ((unsigned char)p[0], 0xAB);
    ASSERT_INT_EQ((unsigned char)p[big - 1], 0xAB);
    ASSERT_TRUE(neverc_arena_num_chunks(a) >= 2);
    neverc_arena_free(a);
}

static void test_many_small_allocs(void) {
    printf("[many_small_allocs]\n");
    neverc_arena_t *a = neverc_arena_new();
    for (int i = 0; i < 10000; i++) {
        int *p = (int *)neverc_arena_alloc(a, sizeof(int));
        ASSERT_TRUE(p != NULL);
        *p = i;
    }
    ASSERT_TRUE(neverc_arena_bytes_allocated(a) >= 10000 * sizeof(int));
    neverc_arena_free(a);
}

static void test_reset(void) {
    printf("[reset]\n");
    neverc_arena_t *a = neverc_arena_new();
    for (int i = 0; i < 1000; i++)
        neverc_arena_alloc(a, 64);
    ASSERT_TRUE(neverc_arena_bytes_allocated(a) >= 64000);

    neverc_arena_reset(a);
    ASSERT_INT_EQ((long long)neverc_arena_bytes_allocated(a), 0);
    ASSERT_INT_EQ((long long)neverc_arena_num_chunks(a), 1);

    int *p = (int *)neverc_arena_alloc(a, sizeof(int));
    ASSERT_TRUE(p != NULL);
    *p = 99;
    ASSERT_INT_EQ(*p, 99);
    neverc_arena_free(a);
}

static void test_aligned_alloc(void) {
    printf("[aligned_alloc]\n");
    neverc_arena_t *a = neverc_arena_new();
    for (size_t align = 1; align <= 256; align *= 2) {
        void *p = neverc_arena_alloc_aligned(a, 32, align);
        ASSERT_TRUE(p != NULL);
        ASSERT_INT_EQ((size_t)p % align, 0);
    }
    neverc_arena_free(a);
}

static void test_invalid_and_overflow_requests(void) {
    printf("[invalid_and_overflow_requests]\n");

    neverc_arena_t *a = neverc_arena_new();
    size_t chunks = neverc_arena_num_chunks(a);
    ASSERT_TRUE(neverc_arena_alloc_aligned(a, 1, 3) == NULL);
    ASSERT_INT_EQ(neverc_arena_bytes_allocated(a), 0);
    ASSERT_INT_EQ(neverc_arena_num_chunks(a), chunks);
    neverc_arena_free(a);

    a = neverc_arena_new();
    chunks = neverc_arena_num_chunks(a);
    ASSERT_TRUE(neverc_arena_calloc(a, SIZE_MAX / 2 + 2, 2) == NULL);
    ASSERT_INT_EQ(neverc_arena_bytes_allocated(a), 0);
    ASSERT_INT_EQ(neverc_arena_num_chunks(a), chunks);
    neverc_arena_free(a);

    a = neverc_arena_new();
    chunks = neverc_arena_num_chunks(a);
    ASSERT_TRUE(neverc_arena_alloc_aligned(a, SIZE_MAX, 8) == NULL);
    ASSERT_INT_EQ(neverc_arena_bytes_allocated(a), 0);
    ASSERT_INT_EQ(neverc_arena_num_chunks(a), chunks);
    neverc_arena_free(a);

    ASSERT_TRUE(neverc_arena_alloc_aligned(NULL, 8, 8) == NULL);
    ASSERT_TRUE(neverc_arena_alloc(NULL, 8) == NULL);
    ASSERT_TRUE(neverc_arena_calloc(NULL, 1, 8) == NULL);
}

int main(void) {
    test_basic_alloc();
    test_calloc();
    test_strdup();
    test_memdup();
    test_large_alloc();
    test_many_small_allocs();
    test_reset();
    test_aligned_alloc();
    test_invalid_and_overflow_requests();
    printf("\narena: %d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}

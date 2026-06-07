#include "neverc/std/context.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define ASSERT_INT_EQ(expr, expected) do { int _v=(int)(expr); int _e=(int)(expected); tests_run++; if(_v==_e)tests_passed++; else{tests_failed++; printf("  FAIL: %s=%d, expected %d (line %d)\n",#expr,_v,_e,__LINE__);}} while(0)
#define ASSERT_TRUE(expr) do { tests_run++; if(expr)tests_passed++; else{tests_failed++; printf("  FAIL: %s (line %d)\n",#expr,__LINE__);}} while(0)

static void test_background(void) {
    printf("[background]\n");
    neverc_context_t *ctx = neverc_context_background();
    ASSERT_TRUE(ctx != NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 0);
    ASSERT_TRUE(neverc_context_err(ctx) == NULL);
    ASSERT_INT_EQ(neverc_context_deadline(ctx), 0);
    neverc_context_free(ctx);
}

static void test_with_value(void) {
    printf("[with_value]\n");
    neverc_context_t *bg = neverc_context_background();
    int val = 42;
    neverc_context_t *ctx = neverc_context_with_value(bg, "key1", &val);
    ASSERT_TRUE(ctx != NULL);

    const int *got = (const int *)neverc_context_value(ctx, "key1");
    ASSERT_TRUE(got != NULL);
    ASSERT_INT_EQ(*got, 42);
    ASSERT_TRUE(neverc_context_value(ctx, "nonexistent") == NULL);

    double val2 = 3.14;
    neverc_context_t *ctx2 = neverc_context_with_value(ctx, "key2", &val2);
    ASSERT_TRUE(neverc_context_value(ctx2, "key1") != NULL);
    ASSERT_TRUE(neverc_context_value(ctx2, "key2") != NULL);

    neverc_context_free(ctx2);
    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_with_timeout(void) {
    printf("[with_timeout]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *ctx = neverc_context_with_timeout(bg, 1, NULL);
    ASSERT_TRUE(ctx != NULL);
    ASSERT_TRUE(neverc_context_deadline(ctx) > 0);

    /* Sleep briefly then check if timeout expired (1ms timeout) */
#if defined(_WIN32)
    Sleep(10);
#else
    usleep(10000);
#endif
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    ASSERT_TRUE(neverc_context_err(ctx) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(ctx), "context deadline exceeded") == 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_not_done(void) {
    printf("[not_done]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *ctx = neverc_context_with_timeout(bg, 10000, NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 0);
    ASSERT_TRUE(neverc_context_err(ctx) == NULL);
    neverc_context_free(ctx);
    neverc_context_free(bg);
}

int main(void) {
    printf("=== NeverC context Tests ===\n");
    test_background();
    test_with_value();
    test_with_timeout();
    test_not_done();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}

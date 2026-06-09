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

static void test_with_cancel(void) {
    printf("[with_cancel]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);
    ASSERT_TRUE(ctx != NULL);
    ASSERT_TRUE(cancel != NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 0);
    ASSERT_TRUE(neverc_context_err(ctx) == NULL);

    cancel();
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    ASSERT_TRUE(neverc_context_err(ctx) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(ctx), "context canceled") == 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_cancel_propagates_to_child(void) {
    printf("[cancel_propagates]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *parent = neverc_context_with_cancel(bg, &cancel);
    neverc_context_t *child = neverc_context_with_value(parent, "k", "v");

    ASSERT_INT_EQ(neverc_context_done(child), 0);
    cancel();
    ASSERT_INT_EQ(neverc_context_done(parent), 1);
    ASSERT_INT_EQ(neverc_context_done(child), 1);

    neverc_context_free(child);
    neverc_context_free(parent);
    neverc_context_free(bg);
}

static void test_multiple_cancels(void) {
    printf("[multiple_cancels]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t c1 = NULL, c2 = NULL, c3 = NULL;
    neverc_context_t *ctx1 = neverc_context_with_cancel(bg, &c1);
    neverc_context_t *ctx2 = neverc_context_with_cancel(bg, &c2);
    neverc_context_t *ctx3 = neverc_context_with_cancel(bg, &c3);

    ASSERT_TRUE(c1 != NULL && c2 != NULL && c3 != NULL);
    ASSERT_TRUE(c1 != c2 && c2 != c3);

    c2();
    ASSERT_INT_EQ(neverc_context_done(ctx1), 0);
    ASSERT_INT_EQ(neverc_context_done(ctx2), 1);
    ASSERT_INT_EQ(neverc_context_done(ctx3), 0);

    c1();
    c3();
    ASSERT_INT_EQ(neverc_context_done(ctx1), 1);
    ASSERT_INT_EQ(neverc_context_done(ctx3), 1);

    neverc_context_free(ctx3);
    neverc_context_free(ctx2);
    neverc_context_free(ctx1);
    neverc_context_free(bg);
}

static void test_cancel_idempotent(void) {
    printf("[cancel_idempotent]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);

    cancel();
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    cancel();
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_without_cancel(void) {
    printf("[without_cancel]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *parent = neverc_context_with_cancel(bg, &cancel);

    neverc_context_t *detached = neverc_context_without_cancel(parent);
    ASSERT_INT_EQ(neverc_context_done(detached), 0);

    cancel();
    ASSERT_INT_EQ(neverc_context_done(parent), 1);
    ASSERT_INT_EQ(neverc_context_done(detached), 0);
    ASSERT_TRUE(neverc_context_err(detached) == NULL);

    neverc_context_free(detached);
    neverc_context_free(parent);
    neverc_context_free(bg);
}

static void test_without_cancel_value(void) {
    printf("[without_cancel_value]\n");
    neverc_context_t *bg = neverc_context_background();
    int val = 42;
    neverc_context_t *vctx = neverc_context_with_value(bg, "mykey", &val);
    neverc_context_t *detached = neverc_context_without_cancel(vctx);

    const void *got = neverc_context_value(detached, "mykey");
    ASSERT_TRUE(got != NULL);
    ASSERT_INT_EQ(*(const int *)got, 42);

    neverc_context_free(detached);
    neverc_context_free(vctx);
    neverc_context_free(bg);
}

static void test_with_cancel_cause(void) {
    printf("[with_cancel_cause]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel_cause(bg, &cancel, "user abort");
    ASSERT_TRUE(ctx != NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 0);

    cancel();
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    const char *cause = neverc_context_cause(ctx);
    ASSERT_TRUE(cause != NULL);
    ASSERT_TRUE(strcmp(cause, "user abort") == 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_with_timeout_cause(void) {
    printf("[with_timeout_cause]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *ctx = neverc_context_with_timeout_cause(bg, 1, NULL, "slow query");
    ASSERT_TRUE(ctx != NULL);

#if defined(_WIN32)
    Sleep(10);
#else
    usleep(10000);
#endif
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    const char *cause = neverc_context_cause(ctx);
    ASSERT_TRUE(cause != NULL);
    ASSERT_TRUE(strcmp(cause, "slow query") == 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_with_deadline_cause(void) {
    printf("[with_deadline_cause]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *ctx = neverc_context_with_deadline_cause(bg, 1, NULL, "past deadline");
    ASSERT_TRUE(ctx != NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    const char *cause = neverc_context_cause(ctx);
    ASSERT_TRUE(cause != NULL);
    ASSERT_TRUE(strcmp(cause, "past deadline") == 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static volatile int g_after_called = 0;
static void after_cb(void) { g_after_called = 1; }

static void test_after_func(void) {
    printf("[after_func]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);

    g_after_called = 0;
    neverc_context_stop_func_t stop = neverc_context_after_func(ctx, after_cb);
    ASSERT_TRUE(stop != NULL);

    ASSERT_INT_EQ(g_after_called, 0);
    cancel();
#if defined(_WIN32)
    Sleep(50);
#else
    usleep(50000);
#endif
    ASSERT_INT_EQ(g_after_called, 1);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_after_func_stop(void) {
    printf("[after_func_stop]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);

    g_after_called = 0;
    neverc_context_stop_func_t stop = neverc_context_after_func(ctx, after_cb);
    ASSERT_TRUE(stop != NULL);

    int stopped = stop();
    ASSERT_INT_EQ(stopped, 1);

    cancel();
#if defined(_WIN32)
    Sleep(50);
#else
    usleep(50000);
#endif
    ASSERT_INT_EQ(g_after_called, 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

int main(void) {
    printf("=== NeverC context Tests ===\n");
    test_background();
    test_with_value();
    test_with_timeout();
    test_not_done();
    test_with_cancel();
    test_cancel_propagates_to_child();
    test_multiple_cancels();
    test_cancel_idempotent();
    test_without_cancel();
    test_without_cancel_value();
    test_with_cancel_cause();
    test_with_timeout_cause();
    test_with_deadline_cause();
    test_after_func();
    test_after_func_stop();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}

#include "neverc/std/context.h"
#include "neverc/std/_platform.h"
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

static void test_timeout_bounds_and_deadline_precedence(void) {
    printf("[timeout_bounds_deadline_precedence]\n");
    neverc_context_t *bg = neverc_context_background();

    neverc_context_t *future =
        neverc_context_with_timeout(bg, INT64_MAX, NULL);
    ASSERT_TRUE(future != NULL);
    ASSERT_TRUE(neverc_context_deadline(future) == INT64_MAX);
    ASSERT_INT_EQ(neverc_context_done(future), 0);

    neverc_context_t *past =
        neverc_context_with_timeout(bg, INT64_MIN, NULL);
    ASSERT_TRUE(past != NULL);
    ASSERT_TRUE(neverc_context_deadline(past) > 0);
    ASSERT_INT_EQ(neverc_context_done(past), 1);
    ASSERT_TRUE(neverc_context_err(past) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(past),
                       "context deadline exceeded") == 0);

    neverc_context_cancel_handle_t *past_handle = NULL;
    neverc_context_t *past_handled =
        neverc_context_with_timeout_handle(bg, INT64_MIN, &past_handle);
    ASSERT_TRUE(past_handled != NULL && past_handle != NULL);
    ASSERT_TRUE(neverc_context_deadline(past_handled) > 0);
    ASSERT_INT_EQ(neverc_context_done(past_handled), 1);

    neverc_context_t *past_cause =
        neverc_context_with_timeout_cause(bg, INT64_MIN, NULL, "already late");
    ASSERT_TRUE(past_cause != NULL);
    ASSERT_INT_EQ(neverc_context_done(past_cause), 1);
    ASSERT_TRUE(neverc_context_cause(past_cause) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_cause(past_cause), "already late") == 0);

    neverc_context_t *past_deadline =
        neverc_context_with_deadline(bg, 0, NULL);
    ASSERT_TRUE(past_deadline != NULL);
    ASSERT_INT_EQ(neverc_context_done(past_deadline), 1);
    ASSERT_TRUE(neverc_context_err(past_deadline) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(past_deadline),
                       "context deadline exceeded") == 0);
    neverc_context_t *neg_deadline =
        neverc_context_with_deadline(bg, -1, NULL);
    ASSERT_TRUE(neg_deadline != NULL);
    ASSERT_INT_EQ(neverc_context_done(neg_deadline), 1);
    neverc_context_free(neg_deadline);
    neverc_context_free(past_deadline);

    neverc_context_t *parent =
        neverc_context_with_deadline(bg, INT64_MAX - 100, NULL);
    neverc_context_t *child =
        neverc_context_with_deadline(parent, INT64_MAX, NULL);
    ASSERT_TRUE(parent != NULL && child != NULL);
    ASSERT_TRUE(neverc_context_deadline(child) == INT64_MAX - 100);

    neverc_context_cancel_handle_t *handle = NULL;
    neverc_context_t *handled = neverc_context_with_timeout_handle(
        bg, INT64_MAX, &handle);
    ASSERT_TRUE(handled != NULL && handle != NULL);
    ASSERT_TRUE(neverc_context_deadline(handled) == INT64_MAX);

    neverc_context_cancel_handle_free(handle);
    neverc_context_free(handled);
    neverc_context_free(child);
    neverc_context_free(parent);
    neverc_context_free(past_cause);
    neverc_context_cancel_handle_free(past_handle);
    neverc_context_free(past_handled);
    neverc_context_free(past);
    neverc_context_free(future);
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

static void test_parent_cancel_outranks_later_child_deadline(void) {
    printf("[parent_cancel_outranks_child_deadline]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *parent =
        neverc_context_with_cancel_cause(bg, &cancel, "parent canceled");
    neverc_context_t *timed = neverc_context_with_timeout_cause(
        parent, 20, NULL, "child timeout");
    neverc_context_t *child =
        neverc_context_with_value(timed, "kept", "yes");

    cancel();
    ASSERT_INT_EQ(neverc_context_done(child), 1);
    ASSERT_TRUE(neverc_context_err(child) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(child), "context canceled") == 0);
    ASSERT_TRUE(neverc_context_cause(child) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_cause(child), "parent canceled") == 0);

#if defined(_WIN32)
    Sleep(80);
#else
    usleep(80000);
#endif
    ASSERT_INT_EQ(neverc_context_done(child), 1);
    ASSERT_TRUE(neverc_context_err(child) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(child), "context canceled") == 0);
    ASSERT_TRUE(neverc_context_cause(child) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_cause(child), "parent canceled") == 0);
    const char *kept = (const char *)neverc_context_value(child, "kept");
    ASSERT_TRUE(kept != NULL);
    if (kept)
        ASSERT_TRUE(strcmp(kept, "yes") == 0);

    neverc_context_free(child);
    neverc_context_free(timed);
    neverc_context_free(parent);
    neverc_context_free(bg);
}

static void test_deadline_outranks_later_cancel(void) {
    printf("[deadline_outranks_later_cancel]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_timeout_cause(bg, 1, &cancel, "already late");
    ASSERT_TRUE(ctx != NULL);
    ASSERT_TRUE(cancel != NULL);

#if defined(_WIN32)
    Sleep(10);
#else
    usleep(10000);
#endif
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    ASSERT_TRUE(neverc_context_err(ctx) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(ctx),
                       "context deadline exceeded") == 0);
    ASSERT_TRUE(neverc_context_cause(ctx) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_cause(ctx), "already late") == 0);

    cancel();
    ASSERT_TRUE(neverc_context_err(ctx) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(ctx),
                       "context deadline exceeded") == 0);
    ASSERT_TRUE(neverc_context_cause(ctx) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_cause(ctx), "already late") == 0);

    neverc_context_cancel_handle_t *handle = NULL;
    neverc_context_t *handled =
        neverc_context_with_timeout_handle(bg, 1, &handle);
    ASSERT_TRUE(handled != NULL && handle != NULL);
#if defined(_WIN32)
    Sleep(10);
#else
    usleep(10000);
#endif
    ASSERT_TRUE(neverc_context_err(handled) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(handled),
                       "context deadline exceeded") == 0);
    neverc_context_cancel_handle_cancel(handle);
    ASSERT_TRUE(neverc_context_err(handled) != NULL);
    ASSERT_TRUE(strcmp(neverc_context_err(handled),
                       "context deadline exceeded") == 0);

    neverc_context_cancel_handle_free(handle);
    neverc_context_free(handled);
    neverc_context_free(ctx);
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

static void test_cancel_handle_not_rebound_while_context_alive(void) {
    printf("[cancel_handle_not_rebound]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t first_cancel = NULL;
    neverc_context_t *first =
        neverc_context_with_cancel(bg, &first_cancel);
    ASSERT_TRUE(first != NULL);
    ASSERT_TRUE(first_cancel != NULL);

    first_cancel();
    ASSERT_INT_EQ(neverc_context_done(first), 1);

    neverc_cancel_func_t second_cancel = NULL;
    neverc_context_t *second =
        neverc_context_with_cancel(bg, &second_cancel);
    ASSERT_TRUE(second != NULL);
    ASSERT_TRUE(second_cancel != NULL);
    ASSERT_TRUE(first_cancel != second_cancel);

    first_cancel();
    ASSERT_INT_EQ(neverc_context_done(second), 0);

    second_cancel();
    ASSERT_INT_EQ(neverc_context_done(second), 1);

    neverc_context_free(second);
    neverc_context_free(first);
    neverc_context_free(bg);
}

static void test_cancel_slots_released_on_free(void) {
    printf("[cancel_slots_released_on_free]\n");
    neverc_context_t *bg = neverc_context_background();

    for (int i = 0; i < 96; i++) {
        neverc_cancel_func_t cancel = NULL;
        neverc_context_t *ctx =
            neverc_context_with_cancel(bg, &cancel);
        ASSERT_TRUE(ctx != NULL);
        ASSERT_TRUE(cancel != NULL);
        neverc_context_free(ctx);
    }

    neverc_context_free(bg);
}

static void test_cancel_slot_exhaustion_fails_atomically(void) {
    printf("[cancel_slot_exhaustion]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *contexts[32] = {0};
    neverc_cancel_func_t cancels[32] = {0};

    for (int i = 0; i < 32; i++) {
        contexts[i] = neverc_context_with_cancel(bg, &cancels[i]);
        ASSERT_TRUE(contexts[i] != NULL);
        ASSERT_TRUE(cancels[i] != NULL);
    }

    neverc_cancel_func_t exhausted_cancel = cancels[0];
    neverc_context_t *exhausted =
        neverc_context_with_cancel(bg, &exhausted_cancel);
    ASSERT_TRUE(exhausted == NULL);
    ASSERT_TRUE(exhausted_cancel == NULL);

    for (int i = 0; i < 32; i++)
        neverc_context_free(contexts[i]);
    neverc_context_free(bg);
}

static void test_explicit_cancel_handles_have_no_global_slot_limit(void) {
    printf("[explicit_cancel_handles_scale]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *contexts[96] = {0};
    neverc_context_cancel_handle_t *handles[96] = {0};

    for (int i = 0; i < 96; i++) {
        contexts[i] =
            neverc_context_with_cancel_handle(bg, &handles[i]);
        ASSERT_TRUE(contexts[i] != NULL);
        ASSERT_TRUE(handles[i] != NULL);
    }

    for (int i = 0; i < 96; i++) {
        neverc_context_cancel_handle_cancel(handles[i]);
        ASSERT_INT_EQ(neverc_context_done(contexts[i]), 1);
        neverc_context_cancel_handle_free(handles[i]);
        neverc_context_free(contexts[i]);
    }

    neverc_context_free(bg);
}

static void test_explicit_timeout_and_deadline_handles(void) {
    printf("[explicit_timeout_deadline_handles]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_cancel_handle_t *timeout_cancel = NULL;
    neverc_context_cancel_handle_t *deadline_cancel = NULL;

    neverc_context_t *timeout = neverc_context_with_timeout_handle(
        bg, 60000, &timeout_cancel);
    ASSERT_TRUE(timeout != NULL);
    ASSERT_TRUE(timeout_cancel != NULL);
    ASSERT_INT_EQ(neverc_context_done(timeout), 0);
    neverc_context_cancel_handle_cancel(timeout_cancel);
    ASSERT_INT_EQ(neverc_context_done(timeout), 1);

    neverc_context_t *deadline = neverc_context_with_deadline_handle(
        bg, INT64_MAX, &deadline_cancel);
    ASSERT_TRUE(deadline != NULL);
    ASSERT_TRUE(deadline_cancel != NULL);
    ASSERT_INT_EQ(neverc_context_done(deadline), 0);
    neverc_context_cancel_handle_cancel(deadline_cancel);
    ASSERT_INT_EQ(neverc_context_done(deadline), 1);

    ASSERT_TRUE(neverc_context_with_timeout_handle(bg, 1, NULL) == NULL);
    ASSERT_TRUE(neverc_context_with_deadline_handle(bg, 1, NULL) == NULL);

    neverc_context_cancel_handle_free(deadline_cancel);
    neverc_context_free(deadline);
    neverc_context_cancel_handle_free(timeout_cancel);
    neverc_context_free(timeout);
    neverc_context_free(bg);
}

static void test_children_and_cancel_handles_retain_context_lifetime(void) {
    printf("[context_parent_lifetime]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_cancel_handle_t *handle = NULL;
    neverc_context_t *parent =
        neverc_context_with_cancel_handle(bg, &handle);
    neverc_context_t *child =
        neverc_context_with_value(parent, "retained", "yes");
    uintptr_t parent_addr = (uintptr_t)parent;

    neverc_context_free(parent);
    neverc_context_free(bg);

    neverc_context_t *churn[128] = {0};
    int parent_storage_reused = 0;
    for (int i = 0; i < 128; i++) {
        churn[i] = neverc_context_background();
        if ((uintptr_t)churn[i] == parent_addr)
            parent_storage_reused = 1;
    }

    ASSERT_INT_EQ(parent_storage_reused, 0);
    neverc_context_cancel_handle_cancel(handle);
    ASSERT_INT_EQ(neverc_context_done(child), 1);
    const char *retained =
        (const char *)neverc_context_value(child, "retained");
    ASSERT_TRUE(retained != NULL);
    if (retained)
        ASSERT_TRUE(strcmp(retained, "yes") == 0);

    neverc_context_free(child);
    neverc_context_cancel_handle_free(handle);
    for (int i = 0; i < 128; i++)
        neverc_context_free(churn[i]);
}

static void test_without_cancel(void) {
    printf("[without_cancel]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *parent = neverc_context_with_cancel_cause(
        bg, &cancel, "parent canceled");
    neverc_context_t *timed =
        neverc_context_with_deadline(parent, INT64_MAX, NULL);

    neverc_context_t *detached = neverc_context_without_cancel(timed);
    ASSERT_INT_EQ(neverc_context_done(detached), 0);
    ASSERT_INT_EQ(neverc_context_deadline(detached), 0);

    cancel();
    ASSERT_INT_EQ(neverc_context_done(parent), 1);
    ASSERT_INT_EQ(neverc_context_done(detached), 0);
    ASSERT_TRUE(neverc_context_err(detached) == NULL);
    ASSERT_TRUE(neverc_context_cause(detached) == NULL);

    neverc_context_free(detached);
    neverc_context_free(timed);
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

static void test_deep_chain_is_iterative(void) {
    printf("[deep_chain_iterative]\n");
    neverc_context_t *ctx = neverc_context_background();
    ASSERT_TRUE(ctx != NULL);

    for (int i = 0; i < 100000 && ctx; i++) {
        neverc_context_t *child =
            neverc_context_with_value(ctx, "depth", (const void *)1);
        neverc_context_free(ctx);
        ctx = child;
    }

    ASSERT_TRUE(ctx != NULL);
    ASSERT_TRUE(neverc_context_value(ctx, "missing") == NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 0);
    ASSERT_INT_EQ(neverc_context_deadline(ctx), 0);
    neverc_context_free(ctx);
}

static void test_with_cancel_cause(void) {
    printf("[with_cancel_cause]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel_cause(bg, &cancel, "user abort");
    ASSERT_TRUE(ctx != NULL);
    ASSERT_INT_EQ(neverc_context_done(ctx), 0);
    ASSERT_TRUE(neverc_context_cause(ctx) == NULL);

    neverc_context_t *child =
        neverc_context_with_value(ctx, "child", "value");

    cancel();
    ASSERT_INT_EQ(neverc_context_done(ctx), 1);
    const char *cause = neverc_context_cause(ctx);
    ASSERT_TRUE(cause != NULL);
    ASSERT_TRUE(strcmp(cause, "user abort") == 0);
    cause = neverc_context_cause(child);
    ASSERT_TRUE(cause != NULL);
    ASSERT_TRUE(strcmp(cause, "user abort") == 0);

    neverc_context_free(child);
    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_with_timeout_cause(void) {
    printf("[with_timeout_cause]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *ctx = neverc_context_with_timeout_cause(bg, 1, NULL, "slow query");
    ASSERT_TRUE(ctx != NULL);
    ASSERT_TRUE(neverc_context_cause(ctx) == NULL);

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

static volatile int32_t g_after_called = 0;
static void after_cb(void) { NEVERC_ATOMIC_STORE32(&g_after_called, 1); }
static neverc_context_t *g_after_self_free_ctx = NULL;
static volatile int32_t g_after_self_free_done = 0;

static void after_self_free_cb(void) {
    neverc_context_free(g_after_self_free_ctx);
    g_after_self_free_ctx = NULL;
    NEVERC_ATOMIC_STORE32(&g_after_self_free_done, 1);
}

static void test_after_func(void) {
    printf("[after_func]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);

    NEVERC_ATOMIC_STORE32(&g_after_called, 0);
    neverc_context_stop_func_t stop = neverc_context_after_func(ctx, after_cb);
    ASSERT_TRUE(stop != NULL);

    ASSERT_INT_EQ(NEVERC_ATOMIC_LOAD32(&g_after_called), 0);
    cancel();
#if defined(_WIN32)
    Sleep(50);
#else
    usleep(50000);
#endif
    ASSERT_INT_EQ(NEVERC_ATOMIC_LOAD32(&g_after_called), 1);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_after_func_stop(void) {
    printf("[after_func_stop]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);

    NEVERC_ATOMIC_STORE32(&g_after_called, 0);
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
    ASSERT_INT_EQ(NEVERC_ATOMIC_LOAD32(&g_after_called), 0);

    neverc_context_free(ctx);
    neverc_context_free(bg);
}

static void test_after_func_stopped_before_context_free(void) {
    printf("[after_func_context_free]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_context_t *ctx =
        neverc_context_with_timeout(bg, 10000, NULL);

    NEVERC_ATOMIC_STORE32(&g_after_called, 0);
    neverc_context_stop_func_t stop =
        neverc_context_after_func(ctx, after_cb);
    ASSERT_TRUE(stop != NULL);

    neverc_context_free(ctx);
    ASSERT_INT_EQ(stop(), 0);
#if defined(_WIN32)
    Sleep(10);
#else
    usleep(10000);
#endif
    ASSERT_INT_EQ(NEVERC_ATOMIC_LOAD32(&g_after_called), 0);

    neverc_context_free(bg);
}

static void test_after_func_can_free_own_context(void) {
    printf("[after_func_self_free]\n");
    neverc_context_t *bg = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *ctx = neverc_context_with_cancel(bg, &cancel);
    ASSERT_TRUE(ctx != NULL);
    ASSERT_TRUE(cancel != NULL);

    g_after_self_free_ctx = ctx;
    NEVERC_ATOMIC_STORE32(&g_after_self_free_done, 0);
    neverc_context_stop_func_t stop =
        neverc_context_after_func(ctx, after_self_free_cb);
    ASSERT_TRUE(stop != NULL);

    cancel();
    for (int i = 0; i < 1000 &&
                    !NEVERC_ATOMIC_LOAD32(&g_after_self_free_done); i++) {
#if defined(_WIN32)
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    ASSERT_INT_EQ(NEVERC_ATOMIC_LOAD32(&g_after_self_free_done), 1);
    ASSERT_TRUE(g_after_self_free_ctx == NULL);
    ASSERT_INT_EQ(stop(), 0);

    neverc_context_free(bg);
}

int main(void) {
    printf("=== NeverC context Tests ===\n");
    test_background();
    test_with_value();
    test_with_timeout();
    test_timeout_bounds_and_deadline_precedence();
    test_not_done();
    test_with_cancel();
    test_cancel_propagates_to_child();
    test_parent_cancel_outranks_later_child_deadline();
    test_deadline_outranks_later_cancel();
    test_multiple_cancels();
    test_cancel_idempotent();
    test_cancel_handle_not_rebound_while_context_alive();
    test_cancel_slots_released_on_free();
    test_cancel_slot_exhaustion_fails_atomically();
    test_explicit_cancel_handles_have_no_global_slot_limit();
    test_explicit_timeout_and_deadline_handles();
    test_children_and_cancel_handles_retain_context_lifetime();
    test_without_cancel();
    test_without_cancel_value();
    test_deep_chain_is_iterative();
    test_with_cancel_cause();
    test_with_timeout_cause();
    test_with_deadline_cause();
    test_after_func();
    test_after_func_stop();
    test_after_func_stopped_before_context_free();
    test_after_func_can_free_own_context();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}

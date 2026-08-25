#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int64_t fake_wall_ms;
static int64_t fake_monotonic_ms;

enum {
    TEST_REASON_PAUSE_NONE,
    TEST_REASON_PAUSE_SNAPSHOT,
    TEST_REASON_PAUSE_VISIT
};

static volatile int32_t test_reason_pause_mode;
static volatile int32_t test_reason_paused;
static volatile int32_t test_reason_resume;
static volatile int32_t test_cancel_finished;
static const void *test_reason_pause_context;

static void test_context_reason_snapshot(const void *context,
                                         int64_t cancel_sequence,
                                         int64_t monotonic_ms);
static void test_context_reason_visit(const void *context);

static int64_t test_context_wall_now_ms(void) { return fake_wall_ms; }
static int64_t test_context_monotonic_now_ms(void) {
    return fake_monotonic_ms;
}

/* context.c keeps these hooks private.  Including it here lets this test
 * move the wall and monotonic clocks independently without changing the
 * public context ABI. */
#define NEVERC_CONTEXT_TEST_WALL_NOW_MS test_context_wall_now_ms
#define NEVERC_CONTEXT_TEST_MONOTONIC_NOW_MS test_context_monotonic_now_ms
#define NEVERC_CONTEXT_TEST_REASON_SNAPSHOT(context, sequence, monotonic_ms) \
    test_context_reason_snapshot((context), (sequence), (monotonic_ms))
#define NEVERC_CONTEXT_TEST_REASON_VISIT(context) \
    test_context_reason_visit((context))
#include "../../../std/src/context/context.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void test_context_reason_pause(void) {
    if (!NEVERC_ATOMIC_CAS32(&test_reason_paused, 0, 1))
        return;
    while (!NEVERC_ATOMIC_LOAD32(&test_reason_resume)) {
        /* The controlling thread advances only after cancellation publishes. */
    }
}

static void test_context_reason_snapshot(const void *context,
                                         int64_t cancel_sequence,
                                         int64_t monotonic_ms) {
    (void)context;
    (void)cancel_sequence;
    (void)monotonic_ms;
    if (NEVERC_ATOMIC_LOAD32(&test_reason_pause_mode) ==
        TEST_REASON_PAUSE_SNAPSHOT)
        test_context_reason_pause();
}

static void test_context_reason_visit(const void *context) {
    if (NEVERC_ATOMIC_LOAD32(&test_reason_pause_mode) ==
            TEST_REASON_PAUSE_VISIT &&
        context == test_reason_pause_context)
        test_context_reason_pause();
}

#if defined(NEVERC_PLATFORM_WINDOWS)
typedef HANDLE context_test_thread_t;
typedef DWORD (WINAPI *context_test_thread_fn_t)(LPVOID);
#define CONTEXT_TEST_THREAD(name) static DWORD WINAPI name(LPVOID opaque)
#define CONTEXT_TEST_THREAD_RESULT 0

static int context_test_thread_start(context_test_thread_t *thread,
                                     context_test_thread_fn_t fn,
                                     void *arg) {
    *thread = CreateThread(NULL, 0, fn, arg, 0, NULL);
    return *thread != NULL;
}

static int context_test_thread_join(context_test_thread_t thread) {
    DWORD result = WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return result == WAIT_OBJECT_0;
}
#else
typedef pthread_t context_test_thread_t;
typedef void *(*context_test_thread_fn_t)(void *);
#define CONTEXT_TEST_THREAD(name) static void *name(void *opaque)
#define CONTEXT_TEST_THREAD_RESULT NULL

static int context_test_thread_start(context_test_thread_t *thread,
                                     context_test_thread_fn_t fn,
                                     void *arg) {
    return pthread_create(thread, NULL, fn, arg) == 0;
}

static int context_test_thread_join(context_test_thread_t thread) {
    return pthread_join(thread, NULL) == 0;
}
#endif

typedef struct {
    neverc_context_t *context;
    int read_cause;
    const char *result;
} reason_thread_args_t;

typedef struct {
    neverc_cancel_func_t first;
    neverc_cancel_func_t second;
} cancel_thread_args_t;

CONTEXT_TEST_THREAD(reason_thread_main) {
    reason_thread_args_t *args = (reason_thread_args_t *)opaque;
    args->result = args->read_cause
                       ? neverc_context_cause(args->context)
                       : neverc_context_err(args->context);
    return CONTEXT_TEST_THREAD_RESULT;
}

CONTEXT_TEST_THREAD(cancel_thread_main) {
    cancel_thread_args_t *args = (cancel_thread_args_t *)opaque;
    args->first();
    if (args->second)
        args->second();
    NEVERC_ATOMIC_STORE32(&test_cancel_finished, 1);
    return CONTEXT_TEST_THREAD_RESULT;
}

static int run_reason_cancel_interleave(
    neverc_context_t *context, int read_cause, int pause_mode,
    const void *pause_context, neverc_cancel_func_t first_cancel,
    neverc_cancel_func_t second_cancel, int advance_monotonic,
    int64_t cancel_monotonic_ms, int latch_deadline_after_advance,
    const char **result_out) {
    reason_thread_args_t reason_args = {context, read_cause, NULL};
    cancel_thread_args_t cancel_args = {first_cancel, second_cancel};
    context_test_thread_t reason_thread;
    context_test_thread_t cancel_thread;

    test_reason_pause_context = pause_context;
    NEVERC_ATOMIC_STORE32(&test_reason_paused, 0);
    NEVERC_ATOMIC_STORE32(&test_reason_resume, 0);
    NEVERC_ATOMIC_STORE32(&test_cancel_finished, 0);
    NEVERC_ATOMIC_STORE32(&test_reason_pause_mode, pause_mode);

    if (!context_test_thread_start(&reason_thread, reason_thread_main,
                                   &reason_args)) {
        NEVERC_ATOMIC_STORE32(&test_reason_pause_mode,
                              TEST_REASON_PAUSE_NONE);
        return 0;
    }
    while (!NEVERC_ATOMIC_LOAD32(&test_reason_paused)) {
        /* The test hook is a deterministic barrier, not a time delay. */
    }

    if (advance_monotonic)
        fake_monotonic_ms = cancel_monotonic_ms;
    int deadline_latched =
        !latch_deadline_after_advance || neverc_context_done(context);
    if (!context_test_thread_start(&cancel_thread, cancel_thread_main,
                                   &cancel_args)) {
        NEVERC_ATOMIC_STORE32(&test_reason_resume, 1);
        (void)context_test_thread_join(reason_thread);
        NEVERC_ATOMIC_STORE32(&test_reason_pause_mode,
                              TEST_REASON_PAUSE_NONE);
        return 0;
    }
    while (!NEVERC_ATOMIC_LOAD32(&test_cancel_finished)) {
        /* Resume only after both cancellation publications are complete. */
    }

    NEVERC_ATOMIC_STORE32(&test_reason_resume, 1);
    int reason_joined = context_test_thread_join(reason_thread);
    int cancel_joined = context_test_thread_join(cancel_thread);
    NEVERC_ATOMIC_STORE32(&test_reason_pause_mode,
                          TEST_REASON_PAUSE_NONE);
    test_reason_pause_context = NULL;
    *result_out = reason_args.result;
    return reason_joined && cancel_joined && deadline_latched;
}

static int check_relative_timeout_uses_monotonic_clock(void) {
    fake_wall_ms = 100000;
    fake_monotonic_ms = 5000;

    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context =
        neverc_context_with_timeout(background, 100, NULL);
    CHECK(background != NULL && context != NULL);
    CHECK(neverc_context_deadline(context) == 100100);

    /* Wall-clock rollback and forward jumps must not change a duration. */
    fake_wall_ms = 1;
    fake_monotonic_ms = 5099;
    CHECK(neverc_context_done(context) == 0);
    fake_wall_ms = INT64_MAX;
    CHECK(neverc_context_done(context) == 0);

    fake_monotonic_ms = 5100;
    CHECK(neverc_context_done(context) == 1);
    CHECK(strcmp(neverc_context_err(context),
                 "context deadline exceeded") == 0);

    neverc_context_free(context);
    neverc_context_free(background);
    return 0;
}

static int check_absolute_deadline_becomes_monotonic_duration(void) {
    fake_wall_ms = 200000;
    fake_monotonic_ms = 8000;

    neverc_context_t *background = neverc_context_background();
    neverc_context_t *context =
        neverc_context_with_deadline(background, 200075, NULL);
    CHECK(background != NULL && context != NULL);
    CHECK(neverc_context_deadline(context) == 200075);

    fake_wall_ms = 900000;
    fake_monotonic_ms = 8074;
    CHECK(neverc_context_done(context) == 0);
    fake_wall_ms = 2;
    CHECK(neverc_context_done(context) == 0);

    fake_monotonic_ms = 8075;
    CHECK(neverc_context_done(context) == 1);

    neverc_context_free(context);
    neverc_context_free(background);
    return 0;
}

static int check_nonpositive_deadline_is_immediate_with_negative_wall(void) {
    fake_wall_ms = -500;
    fake_monotonic_ms = 8500;

    neverc_context_t *background = neverc_context_background();
    neverc_context_t *zero =
        neverc_context_with_deadline(background, 0, NULL);
    CHECK(background != NULL && zero != NULL);
    CHECK(neverc_context_deadline(zero) == 1);
    CHECK(neverc_context_done(zero) == 1);
    CHECK(strcmp(neverc_context_err(zero),
                 "context deadline exceeded") == 0);
    neverc_context_free(zero);

    fake_wall_ms = INT64_MIN;
    fake_monotonic_ms = 8600;
    neverc_context_t *minimum = neverc_context_with_deadline_cause(
        background, INT64_MIN, NULL, "minimum deadline");
    CHECK(minimum != NULL);
    CHECK(neverc_context_deadline(minimum) == 1);
    CHECK(neverc_context_done(minimum) == 1);
    CHECK(strcmp(neverc_context_cause(minimum),
                 "minimum deadline") == 0);

    neverc_context_free(minimum);
    neverc_context_free(background);
    return 0;
}

static int check_posix_monotonic_conversion_saturates(void) {
#if !defined(NEVERC_PLATFORM_WINDOWS)
    uint64_t seconds_limit = (uint64_t)INT64_MAX / 1000;
    uint64_t millis_limit = (uint64_t)INT64_MAX % 1000;

    CHECK(context_posix_monotonic_ms(seconds_limit,
                                     (millis_limit - 1) * 1000000) ==
          INT64_MAX - 1);
    CHECK(context_posix_monotonic_ms(seconds_limit,
                                     millis_limit * 1000000) == INT64_MAX);
    CHECK(context_posix_monotonic_ms(seconds_limit,
                                     (millis_limit + 1) * 1000000) ==
          INT64_MAX);
    CHECK(context_posix_monotonic_ms(seconds_limit + 1, 0) == INT64_MAX);
#endif
    return 0;
}

static int check_same_tick_cancel_order_is_first_wins(void) {
    fake_wall_ms = 300000;
    fake_monotonic_ms = 9000;

    neverc_context_t *background = neverc_context_background();
    neverc_cancel_func_t parent_cancel = NULL;
    neverc_cancel_func_t child_cancel = NULL;
    neverc_context_t *parent = neverc_context_with_cancel_cause(
        background, &parent_cancel, "parent first");
    neverc_context_t *child = neverc_context_with_cancel_cause(
        parent, &child_cancel, "child second");
    CHECK(background != NULL && parent != NULL && child != NULL);
    CHECK(parent_cancel != NULL && child_cancel != NULL);

    /* Both calls see the same millisecond.  Sequence, not clock resolution,
     * must preserve that the parent cancellation happened first. */
    parent_cancel();
    child_cancel();
    CHECK(strcmp(neverc_context_cause(child), "parent first") == 0);

    neverc_context_free(child);
    neverc_context_free(parent);

    parent_cancel = NULL;
    child_cancel = NULL;
    parent = neverc_context_with_cancel_cause(
        background, &parent_cancel, "parent second");
    child = neverc_context_with_cancel_cause(
        parent, &child_cancel, "child first");
    CHECK(parent != NULL && child != NULL);
    CHECK(parent_cancel != NULL && child_cancel != NULL);

    child_cancel();
    parent_cancel();
    CHECK(strcmp(neverc_context_cause(child), "child first") == 0);

    neverc_context_free(child);
    neverc_context_free(parent);
    neverc_context_free(background);
    return 0;
}

static int check_reason_cancel_chain_uses_one_snapshot(void) {
    fake_wall_ms = 350000;
    fake_monotonic_ms = 9500;

    neverc_context_t *background = neverc_context_background();
    neverc_cancel_func_t parent_cancel = NULL;
    neverc_cancel_func_t child_cancel = NULL;
    neverc_context_t *parent = neverc_context_with_cancel_cause(
        background, &parent_cancel, "parent second");
    neverc_context_t *child = neverc_context_with_cancel_cause(
        parent, &child_cancel, "child first");
    CHECK(background != NULL && parent != NULL && child != NULL);
    CHECK(parent_cancel != NULL && child_cancel != NULL);

    const char *first_result = (const char *)1;
    CHECK(run_reason_cancel_interleave(
        child, 1, TEST_REASON_PAUSE_VISIT, child, child_cancel,
        parent_cancel, 0, 0, 0, &first_result));

    /* The query linearized before either cancellation. It must not combine
     * the already-scanned child with a later parent publication. */
    CHECK(first_result == NULL);
    CHECK(strcmp(neverc_context_cause(child), "child first") == 0);
    CHECK(strcmp(neverc_context_cause(child), "child first") == 0);

    neverc_context_free(child);
    neverc_context_free(parent);
    neverc_context_free(background);
    return 0;
}

static int check_reason_deadline_and_cancel_use_same_snapshot(void) {
    fake_wall_ms = 360000;
    fake_monotonic_ms = 99;

    neverc_context_t *background = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *context = neverc_context_with_deadline_cause(
        background, 360001, &cancel, "deadline first");
    CHECK(background != NULL && context != NULL && cancel != NULL);

    const char *first_result = (const char *)1;
    CHECK(run_reason_cancel_interleave(
        context, 0, TEST_REASON_PAUSE_SNAPSHOT, NULL, cancel, NULL,
        1, 101, 1, &first_result));

    /* The first query sampled now=99. A second reader latches target=100 at
     * 101, then cancel publishes at 101. Neither post-snapshot event may leak
     * into the first result; the next query permanently selects the deadline. */
    CHECK(first_result == NULL);
    CHECK(strcmp(neverc_context_err(context),
                 "context deadline exceeded") == 0);
    CHECK(strcmp(neverc_context_cause(context), "deadline first") == 0);
    CHECK(strcmp(neverc_context_cause(context), "deadline first") == 0);

    neverc_context_free(context);
    neverc_context_free(background);
    return 0;
}

static int check_zero_timeout_keeps_deadline_tie_precedence(void) {
    fake_wall_ms = 400000;
    fake_monotonic_ms = 10000;

    neverc_context_t *background = neverc_context_background();
    neverc_cancel_func_t cancel = NULL;
    neverc_context_t *context = neverc_context_with_timeout_cause(
        background, 0, &cancel, "zero timeout");
    CHECK(background != NULL && context != NULL && cancel != NULL);

    cancel();
    CHECK(strcmp(neverc_context_err(context),
                 "context deadline exceeded") == 0);
    CHECK(strcmp(neverc_context_cause(context), "zero timeout") == 0);

    neverc_context_free(context);
    neverc_context_free(background);
    return 0;
}

int main(void) {
    CHECK(check_relative_timeout_uses_monotonic_clock() == 0);
    CHECK(check_absolute_deadline_becomes_monotonic_duration() == 0);
    CHECK(check_nonpositive_deadline_is_immediate_with_negative_wall() == 0);
    CHECK(check_posix_monotonic_conversion_saturates() == 0);
    CHECK(check_same_tick_cancel_order_is_first_wins() == 0);
    CHECK(check_reason_cancel_chain_uses_one_snapshot() == 0);
    CHECK(check_reason_deadline_and_cancel_use_same_snapshot() == 0);
    CHECK(check_zero_timeout_keeps_deadline_tie_precedence() == 0);
    puts("passed");
    return 0;
}

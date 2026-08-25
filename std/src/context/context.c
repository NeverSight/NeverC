/*
 * NeverC context — cancellation and deadline propagation.
 * Simplified C implementation of Go's context package.
 * Thread-safe using atomics and internal event synchronization.
 */

#include "neverc/std/context.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(NEVERC_PLATFORM_WINDOWS)
  #include <windows.h>
  static int64_t context_system_wall_now_ms(void) {
      FILETIME ft; GetSystemTimeAsFileTime(&ft);
      uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
      return (int64_t)(t / 10000 - 11644473600000LL);
  }
  static int64_t context_system_monotonic_now_ms(void) {
      ULONGLONG value = GetTickCount64();
      return value > (ULONGLONG)INT64_MAX ? INT64_MAX : (int64_t)value;
  }
#else
  #include <pthread.h>
  #include <sys/time.h>
  #include <time.h>
  static int64_t context_posix_monotonic_ms(uint64_t seconds,
                                             uint64_t nanoseconds) {
      uint64_t millis = nanoseconds / 1000000;
      uint64_t limit = (uint64_t)INT64_MAX;
      /* This also covers seconds == INT64_MAX/1000 with a millisecond
       * remainder greater than INT64_MAX%1000. */
      if (millis > limit || seconds > (limit - millis) / 1000)
          return INT64_MAX;
      return (int64_t)(seconds * 1000 + millis);
  }
  static int64_t context_system_wall_now_ms(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  }
  static int64_t context_system_monotonic_now_ms(void) {
      struct timespec ts;
      if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
          return 0;
      if (ts.tv_sec < 0 || ts.tv_nsec < 0)
          return 0;
      return context_posix_monotonic_ms((uint64_t)ts.tv_sec,
                                        (uint64_t)ts.tv_nsec);
  }
#endif

#ifndef NEVERC_CONTEXT_TEST_WALL_NOW_MS
#define NEVERC_CONTEXT_TEST_WALL_NOW_MS context_system_wall_now_ms
#endif
#ifndef NEVERC_CONTEXT_TEST_MONOTONIC_NOW_MS
#define NEVERC_CONTEXT_TEST_MONOTONIC_NOW_MS context_system_monotonic_now_ms
#endif
#ifndef NEVERC_CONTEXT_TEST_REASON_SNAPSHOT
#define NEVERC_CONTEXT_TEST_REASON_SNAPSHOT(context, sequence, monotonic_ms) \
    ((void)0)
#endif
#ifndef NEVERC_CONTEXT_TEST_REASON_VISIT
#define NEVERC_CONTEXT_TEST_REASON_VISIT(context) ((void)0)
#endif

static int64_t context_wall_now_ms(void) {
    return NEVERC_CONTEXT_TEST_WALL_NOW_MS();
}

static int64_t context_monotonic_now_ms(void) {
    int64_t value = NEVERC_CONTEXT_TEST_MONOTONIC_NOW_MS();
    return value < 0 ? 0 : value;
}

static int64_t context_add_duration(int64_t start_ms,
                                    int64_t duration_ms) {
    if (duration_ms <= 0)
        return start_ms;
    if (start_ms > INT64_MAX - duration_ms)
        return INT64_MAX;
    return start_ms + duration_ms;
}

static int64_t context_wall_deadline_after(int64_t wall_now_ms,
                                           int64_t timeout_ms) {
    /* Negative timeouts are already expired; INT64_MIN cannot be added. */
    if (timeout_ms < 0)
        return 1;
    int64_t deadline = context_add_duration(wall_now_ms, timeout_ms);
    return deadline > 0 ? deadline : 1;
}

static int64_t context_remaining_duration(int64_t deadline_ms,
                                          int64_t wall_now_ms) {
    if (deadline_ms <= wall_now_ms)
        return 0;
    /* deadline_ms is positive. Avoid overflow when a test/platform exposes
     * a pre-epoch (negative) wall-clock value. */
    if (wall_now_ms < deadline_ms - INT64_MAX)
        return INT64_MAX;
    return deadline_ms - wall_now_ms;
}

typedef enum {
    CTX_BACKGROUND,
    CTX_CANCEL,
    CTX_TIMEOUT,
    CTX_VALUE,
    CTX_WITHOUT_CANCEL
} ctx_kind_t;

struct neverc_context {
    ctx_kind_t kind;
    neverc_context_t *parent;
    volatile int32_t refs;
    volatile int32_t deadline_latched;
    /* Go WithDeadline on an already-canceled parent records Canceled
     * first; an already-past child deadline must not replace it. */
    int suppress_deadline_err;
#if defined(_MSC_VER) && !defined(__clang__)
    __declspec(align(8)) volatile int64_t cancel_sequence;
#else
    _Alignas(8) volatile int64_t cancel_sequence;
#endif
    int64_t cancel_monotonic_ms;
    int64_t deadline_ms;
    int64_t deadline_monotonic_ms;
    const char *key;
    const void *value;
    const char *cause;
    uint32_t cancel_slot; /* slot index + 1; zero means no cancel handle */
};

struct neverc_context_cancel_handle {
    neverc_context_t *ctx;
};

static neverc_context_t *ctx_retain(neverc_context_t *ctx) {
    if (ctx)
        (void)NEVERC_ATOMIC_ADD32(&ctx->refs, 1);
    return ctx;
}

static const char *ctx_reason(const neverc_context_t *ctx, const char **cause_out);

/* Sequence allocation and publication share one lock.  A cancellation with
 * sequence N is fully published before N+1 can be assigned, so readers can
 * use an atomic sequence load without serializing every parent-chain walk. */
static volatile int32_t g_cancel_order_lock;
static int64_t g_last_cancel_sequence;

static void cancel_order_lock(void) {
    while (!NEVERC_ATOMIC_CAS32(&g_cancel_order_lock, 0, 1)) {
        /* A first cancellation performs only constant-time publication. */
    }
}

static void cancel_order_unlock(void) {
    NEVERC_ATOMIC_STORE32(&g_cancel_order_lock, 0);
}

static int ctx_parent_canceled(const neverc_context_t *parent) {
    /* Go WithDeadlineCause: a parent that is already done (Canceled or
     * DeadlineExceeded) keeps its Err/Cause; the child's expired sentinel
     * must not overwrite it. */
    return ctx_reason(parent, NULL) != NULL;
}

static neverc_context_t *ctx_create(ctx_kind_t kind,
                                    neverc_context_t *parent) {
    neverc_context_t *ctx = (neverc_context_t *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->kind = kind;
    ctx->refs = 1;
    ctx->parent = ctx_retain(parent);
    return ctx;
}

static void ctx_set_timeout(neverc_context_t *ctx, int64_t timeout_ms) {
    int64_t wall_now_ms = context_wall_now_ms();
    int64_t monotonic_now_ms = context_monotonic_now_ms();
    ctx->deadline_ms = context_wall_deadline_after(wall_now_ms, timeout_ms);
    ctx->deadline_monotonic_ms = context_add_duration(
        monotonic_now_ms, timeout_ms < 0 ? 0 : timeout_ms);
}

static void ctx_set_deadline(neverc_context_t *ctx, int64_t deadline_ms) {
    if (deadline_ms <= 0) {
        ctx->deadline_ms = 1;
        ctx->deadline_monotonic_ms = context_monotonic_now_ms();
        return;
    }

    int64_t wall_now_ms = context_wall_now_ms();
    int64_t monotonic_now_ms = context_monotonic_now_ms();
    int64_t remaining_ms = context_remaining_duration(deadline_ms,
                                                      wall_now_ms);
    ctx->deadline_ms = deadline_ms;
    ctx->deadline_monotonic_ms =
        context_add_duration(monotonic_now_ms, remaining_ms);
}

static void ctx_cancel_impl(neverc_context_t *ctx) {
    if (!ctx)
        return;

    cancel_order_lock();
    if (NEVERC_ATOMIC_LOAD64(&ctx->cancel_sequence) == 0) {
        /* Wrapping would make a later cancellation compare earlier.  A
         * process cannot realistically issue 2^63 first cancellations, but
         * fail before violating the strict-order invariant if it ever does. */
        if (g_last_cancel_sequence == INT64_MAX)
            abort();
        ctx->cancel_monotonic_ms = context_monotonic_now_ms();
        NEVERC_ATOMIC_STORE64(&ctx->cancel_sequence,
                              ++g_last_cancel_sequence);
    }
    cancel_order_unlock();
}

/*
 * Cancel trampoline table: C has no closures, so we use a fixed pool of
 * trampoline functions, each bound to a slot in g_cancel_slots[].
 * When with_cancel is called, we assign a free slot and return the
 * corresponding trampoline. Slots remain bound until the context is freed,
 * so repeated calls stay idempotent and cannot target another live context.
 * The callback is invalid after neverc_context_free, like any other handle
 * owned by the context.
 */
#define NEVERC_CANCEL_SLOTS 32
static neverc_context_t *g_cancel_slots[NEVERC_CANCEL_SLOTS];
static volatile int32_t g_cancel_slots_lock;

static void cancel_slots_lock(void) {
    while (!NEVERC_ATOMIC_CAS32(&g_cancel_slots_lock, 0, 1)) {
        /* Slot operations are constant-time; contention should be brief. */
    }
}

static void cancel_slots_unlock(void) {
    NEVERC_ATOMIC_STORE32(&g_cancel_slots_lock, 0);
}

#define CANCEL_TRAMPOLINE(N) \
    static void _cancel_trampoline_##N(void) { \
        cancel_slots_lock(); \
        ctx_cancel_impl(g_cancel_slots[N]); \
        cancel_slots_unlock(); \
    }

CANCEL_TRAMPOLINE(0)  CANCEL_TRAMPOLINE(1)  CANCEL_TRAMPOLINE(2)
CANCEL_TRAMPOLINE(3)  CANCEL_TRAMPOLINE(4)  CANCEL_TRAMPOLINE(5)
CANCEL_TRAMPOLINE(6)  CANCEL_TRAMPOLINE(7)  CANCEL_TRAMPOLINE(8)
CANCEL_TRAMPOLINE(9)  CANCEL_TRAMPOLINE(10) CANCEL_TRAMPOLINE(11)
CANCEL_TRAMPOLINE(12) CANCEL_TRAMPOLINE(13) CANCEL_TRAMPOLINE(14)
CANCEL_TRAMPOLINE(15) CANCEL_TRAMPOLINE(16) CANCEL_TRAMPOLINE(17)
CANCEL_TRAMPOLINE(18) CANCEL_TRAMPOLINE(19) CANCEL_TRAMPOLINE(20)
CANCEL_TRAMPOLINE(21) CANCEL_TRAMPOLINE(22) CANCEL_TRAMPOLINE(23)
CANCEL_TRAMPOLINE(24) CANCEL_TRAMPOLINE(25) CANCEL_TRAMPOLINE(26)
CANCEL_TRAMPOLINE(27) CANCEL_TRAMPOLINE(28) CANCEL_TRAMPOLINE(29)
CANCEL_TRAMPOLINE(30) CANCEL_TRAMPOLINE(31)

static const neverc_cancel_func_t g_cancel_trampolines[NEVERC_CANCEL_SLOTS] = {
    _cancel_trampoline_0,  _cancel_trampoline_1,  _cancel_trampoline_2,
    _cancel_trampoline_3,  _cancel_trampoline_4,  _cancel_trampoline_5,
    _cancel_trampoline_6,  _cancel_trampoline_7,  _cancel_trampoline_8,
    _cancel_trampoline_9,  _cancel_trampoline_10, _cancel_trampoline_11,
    _cancel_trampoline_12, _cancel_trampoline_13, _cancel_trampoline_14,
    _cancel_trampoline_15, _cancel_trampoline_16, _cancel_trampoline_17,
    _cancel_trampoline_18, _cancel_trampoline_19, _cancel_trampoline_20,
    _cancel_trampoline_21, _cancel_trampoline_22, _cancel_trampoline_23,
    _cancel_trampoline_24, _cancel_trampoline_25, _cancel_trampoline_26,
    _cancel_trampoline_27, _cancel_trampoline_28, _cancel_trampoline_29,
    _cancel_trampoline_30, _cancel_trampoline_31,
};

static int alloc_cancel_slot(neverc_context_t *ctx) {
    cancel_slots_lock();
    for (int i = 0; i < NEVERC_CANCEL_SLOTS; i++) {
        if (!g_cancel_slots[i]) {
            g_cancel_slots[i] = ctx;
            ctx->cancel_slot = (uint32_t)i + 1;
            cancel_slots_unlock();
            return i;
        }
    }
    cancel_slots_unlock();
    return -1;
}

static void release_cancel_slot(neverc_context_t *ctx) {
    if (!ctx || ctx->cancel_slot == 0)
        return;

    cancel_slots_lock();
    size_t slot = (size_t)ctx->cancel_slot - 1;
    if (slot < NEVERC_CANCEL_SLOTS && g_cancel_slots[slot] == ctx)
        g_cancel_slots[slot] = NULL;
    ctx->cancel_slot = 0;
    cancel_slots_unlock();
}

static int bind_cancel_func(neverc_context_t *ctx,
                            neverc_cancel_func_t *cancel_out) {
    if (!cancel_out)
        return 0;

    *cancel_out = NULL;
    int slot = alloc_cancel_slot(ctx);
    if (slot < 0)
        return -1;
    *cancel_out = g_cancel_trampolines[slot];
    return 0;
}

neverc_context_t *neverc_context_background(void) {
    return ctx_create(CTX_BACKGROUND, NULL);
}

neverc_context_t *neverc_context_with_cancel(neverc_context_t *parent,
                                              neverc_cancel_func_t *cancel_out) {
    neverc_context_t *ctx = ctx_create(CTX_CANCEL, parent);
    if (!ctx) {
        if (cancel_out) *cancel_out = NULL;
        return NULL;
    }
    if (bind_cancel_func(ctx, cancel_out) != 0) {
        neverc_context_free(ctx);
        return NULL;
    }
    return ctx;
}

neverc_context_t *neverc_context_todo(void) {
    return neverc_context_background();
}

neverc_context_t *neverc_context_with_cancel_cause(neverc_context_t *parent,
                                                    neverc_cancel_func_t *cancel_out,
                                                    const char *cause) {
    neverc_context_t *ctx = ctx_create(CTX_CANCEL, parent);
    if (!ctx) {
        if (cancel_out) *cancel_out = NULL;
        return NULL;
    }
    ctx->cause = cause;
    if (bind_cancel_func(ctx, cancel_out) != 0) {
        neverc_context_free(ctx);
        return NULL;
    }
    return ctx;
}

static neverc_context_t *ctx_with_cancel_handle(
    ctx_kind_t kind, neverc_context_t *parent, int64_t deadline_value,
    int deadline_is_duration,
    neverc_context_cancel_handle_t **cancel_out) {
    if (!cancel_out)
        return NULL;
    *cancel_out = NULL;

    neverc_context_t *ctx = ctx_create(kind, parent);
    if (!ctx)
        return NULL;
    if (kind == CTX_TIMEOUT) {
        if (deadline_is_duration)
            ctx_set_timeout(ctx, deadline_value);
        else
            ctx_set_deadline(ctx, deadline_value);
        if (ctx_parent_canceled(parent))
            ctx->suppress_deadline_err = 1;
    }

    neverc_context_cancel_handle_t *handle =
        (neverc_context_cancel_handle_t *)calloc(1, sizeof(*handle));
    if (!handle) {
        neverc_context_free(ctx);
        return NULL;
    }
    handle->ctx = ctx_retain(ctx);
    *cancel_out = handle;
    return ctx;
}

neverc_context_t *neverc_context_with_cancel_handle(
    neverc_context_t *parent, neverc_context_cancel_handle_t **cancel_out) {
    return ctx_with_cancel_handle(CTX_CANCEL, parent, 0, 0, cancel_out);
}

neverc_context_t *neverc_context_with_timeout_handle(
    neverc_context_t *parent, int64_t timeout_ms,
    neverc_context_cancel_handle_t **cancel_out) {
    return ctx_with_cancel_handle(CTX_TIMEOUT, parent, timeout_ms, 1,
                                  cancel_out);
}

neverc_context_t *neverc_context_with_deadline_handle(
    neverc_context_t *parent, int64_t deadline_ms,
    neverc_context_cancel_handle_t **cancel_out) {
    return ctx_with_cancel_handle(CTX_TIMEOUT, parent, deadline_ms, 0,
                                  cancel_out);
}

void neverc_context_cancel_handle_cancel(
    neverc_context_cancel_handle_t *handle) {
    if (handle)
        ctx_cancel_impl(handle->ctx);
}

void neverc_context_cancel_handle_free(
    neverc_context_cancel_handle_t *handle) {
    if (!handle)
        return;
    neverc_context_t *ctx = handle->ctx;
    handle->ctx = NULL;
    free(handle);
    neverc_context_free(ctx);
}

neverc_context_t *neverc_context_with_timeout(neverc_context_t *parent,
                                               int64_t timeout_ms,
                                               neverc_cancel_func_t *cancel_out) {
    neverc_context_t *ctx = ctx_create(CTX_TIMEOUT, parent);
    if (!ctx) {
        if (cancel_out) *cancel_out = NULL;
        return NULL;
    }
    ctx_set_timeout(ctx, timeout_ms);
    if (ctx_parent_canceled(parent))
        ctx->suppress_deadline_err = 1;
    if (bind_cancel_func(ctx, cancel_out) != 0) {
        neverc_context_free(ctx);
        return NULL;
    }
    return ctx;
}

neverc_context_t *neverc_context_with_deadline(neverc_context_t *parent,
                                                int64_t deadline_ms,
                                                neverc_cancel_func_t *cancel_out) {
    neverc_context_t *ctx = ctx_create(CTX_TIMEOUT, parent);
    if (!ctx) {
        if (cancel_out) *cancel_out = NULL;
        return NULL;
    }
    /* A non-positive absolute deadline is already in the past. */
    ctx_set_deadline(ctx, deadline_ms);
    if (ctx_parent_canceled(parent))
        ctx->suppress_deadline_err = 1;
    if (bind_cancel_func(ctx, cancel_out) != 0) {
        neverc_context_free(ctx);
        return NULL;
    }
    return ctx;
}

neverc_context_t *neverc_context_with_timeout_cause(neverc_context_t *parent,
                                                     int64_t timeout_ms,
                                                     neverc_cancel_func_t *cancel_out,
                                                     const char *cause) {
    neverc_context_t *ctx = ctx_create(CTX_TIMEOUT, parent);
    if (!ctx) {
        if (cancel_out) *cancel_out = NULL;
        return NULL;
    }
    ctx_set_timeout(ctx, timeout_ms);
    ctx->cause = cause;
    if (ctx_parent_canceled(parent))
        ctx->suppress_deadline_err = 1;
    if (bind_cancel_func(ctx, cancel_out) != 0) {
        neverc_context_free(ctx);
        return NULL;
    }
    return ctx;
}

neverc_context_t *neverc_context_with_deadline_cause(neverc_context_t *parent,
                                                      int64_t deadline_ms,
                                                      neverc_cancel_func_t *cancel_out,
                                                      const char *cause) {
    neverc_context_t *ctx = ctx_create(CTX_TIMEOUT, parent);
    if (!ctx) {
        if (cancel_out) *cancel_out = NULL;
        return NULL;
    }
    ctx_set_deadline(ctx, deadline_ms);
    ctx->cause = cause;
    if (ctx_parent_canceled(parent))
        ctx->suppress_deadline_err = 1;
    if (bind_cancel_func(ctx, cancel_out) != 0) {
        neverc_context_free(ctx);
        return NULL;
    }
    return ctx;
}

neverc_context_t *neverc_context_with_value(neverc_context_t *parent,
                                             const char *key, const void *value) {
    neverc_context_t *ctx = ctx_create(CTX_VALUE, parent);
    if (!ctx) return NULL;
    ctx->key = key;
    ctx->value = value;
    return ctx;
}

neverc_context_t *neverc_context_without_cancel(neverc_context_t *parent) {
    neverc_context_t *ctx = ctx_create(CTX_WITHOUT_CANCEL, parent);
    if (!ctx) return NULL;
    return ctx;
}

/* Go records the first cancel/deadline that fires and never overwrites Err.
 * Cancellation order comes from a strict process-wide sequence. Deadline
 * order comes from monotonic targets; wall-clock values are reporting only.
 * The global lock protects only the constant-time snapshot, never the chain
 * walk: every accepted sequence was fully published before this query's now. */
static const char *ctx_reason(const neverc_context_t *ctx, const char **cause_out) {
    const neverc_context_t *cancel_context = NULL;
    const neverc_context_t *deadline_context = NULL;
    int64_t earliest_cancel_sequence = INT64_MAX;
    int64_t earliest_cancel_ms = INT64_MAX;
    int64_t earliest_deadline_ms = INT64_MAX;

    cancel_order_lock();
    int64_t snapshot_cancel_sequence = g_last_cancel_sequence;
    int64_t snapshot_now_ms = context_monotonic_now_ms();
    cancel_order_unlock();
    NEVERC_CONTEXT_TEST_REASON_SNAPSHOT(ctx, snapshot_cancel_sequence,
                                        snapshot_now_ms);

    while (ctx) {
        if (ctx->kind == CTX_WITHOUT_CANCEL)
            break;

        int64_t cancel_sequence = NEVERC_ATOMIC_LOAD64(
            (int64_t *)&ctx->cancel_sequence);
        if (cancel_sequence > 0 &&
            cancel_sequence <= snapshot_cancel_sequence &&
            cancel_sequence < earliest_cancel_sequence) {
            earliest_cancel_sequence = cancel_sequence;
            earliest_cancel_ms = ctx->cancel_monotonic_ms;
            cancel_context = ctx;
        }
        if (ctx->kind == CTX_TIMEOUT &&
            !ctx->suppress_deadline_err) {
            neverc_context_t *mut = (neverc_context_t *)ctx;
            int expired_at_snapshot =
                ctx->deadline_monotonic_ms <= snapshot_now_ms;
            if (expired_at_snapshot &&
                !NEVERC_ATOMIC_LOAD32(&mut->deadline_latched))
                NEVERC_ATOMIC_STORE32(&mut->deadline_latched, 1);
            /* The walk is child-to-parent. <= preserves the established rule
             * that an equal parent deadline supplies the propagated cause.
             * Latch publication by a later reader is not deadline evidence
             * for this query; eligibility always uses snapshot_now_ms. */
            if (expired_at_snapshot &&
                ctx->deadline_monotonic_ms <= earliest_deadline_ms) {
                earliest_deadline_ms = ctx->deadline_monotonic_ms;
                deadline_context = ctx;
            }
        }
        NEVERC_CONTEXT_TEST_REASON_VISIT(ctx);
        ctx = ctx->parent;
    }

    const char *err = NULL;
    const char *cause = NULL;
    if (deadline_context &&
        (!cancel_context || earliest_deadline_ms <= earliest_cancel_ms)) {
        err = "context deadline exceeded";
        cause = deadline_context->cause ? deadline_context->cause : err;
    } else if (cancel_context) {
        err = "context canceled";
        /* Go WithTimeoutCause/WithDeadlineCause: CancelFunc does not attach
         * the timeout cause. CTX_CANCEL still uses WithCancelCause's cause. */
        if (cancel_context->kind == CTX_TIMEOUT)
            cause = err;
        else
            cause = cancel_context->cause ? cancel_context->cause : err;
    }
    if (cause_out)
        *cause_out = cause;
    return err;
}

int neverc_context_done(const neverc_context_t *ctx) {
    return ctx_reason(ctx, NULL) != NULL;
}

const char *neverc_context_err(const neverc_context_t *ctx) {
    return ctx_reason(ctx, NULL);
}

const void *neverc_context_value(const neverc_context_t *ctx, const char *key) {
    while (ctx) {
        if (ctx->kind == CTX_VALUE && ctx->key && key &&
            strcmp(ctx->key, key) == 0)
            return ctx->value;
        ctx = ctx->parent;
    }
    return NULL;
}

const char *neverc_context_cause(const neverc_context_t *ctx) {
    const char *cause = NULL;
    if (!ctx_reason(ctx, &cause))
        return NULL;
    return cause;
}

int64_t neverc_context_deadline(const neverc_context_t *ctx) {
    int64_t earliest = 0;
    while (ctx) {
        if (ctx->kind == CTX_WITHOUT_CANCEL) break;
        if (ctx->kind == CTX_TIMEOUT && ctx->deadline_ms > 0 &&
            (earliest == 0 || ctx->deadline_ms < earliest))
            earliest = ctx->deadline_ms;
        ctx = ctx->parent;
    }
    return earliest;
}

static void ctx_stop_after_funcs(neverc_context_t *ctx);

void neverc_context_free(neverc_context_t *ctx) {
    while (ctx) {
        if (NEVERC_ATOMIC_ADD32(&ctx->refs, -1) != 0)
            return;

        neverc_context_t *parent = ctx->parent;
        ctx_stop_after_funcs(ctx);
        ctx->parent = NULL;
        release_cancel_slot(ctx);
        free(ctx);
        ctx = parent;
    }
}

/*
 * AfterFunc — schedule f() to run when ctx is done.
 * Returns a stop function; calling stop returns 1 if f was prevented
 * from running, 0 if f already ran or is running.
 */

typedef struct {
    neverc_context_t *ctx;
    void (*f)(void);
    volatile int32_t state;
    volatile int32_t thread_finished;
    volatile int32_t ready;
#if defined(NEVERC_PLATFORM_WINDOWS)
    DWORD thread_id;
#else
    pthread_t thread_id;
#endif
} after_func_state_t;

enum {
    AFTER_FUNC_WAITING,
    AFTER_FUNC_STOPPED,
    AFTER_FUNC_RUNNING,
    AFTER_FUNC_FINISHED
};

#if defined(NEVERC_PLATFORM_WINDOWS)
static DWORD WINAPI after_func_thread(LPVOID arg) {
    after_func_state_t *s = (after_func_state_t *)arg;
    while (NEVERC_ATOMIC_LOAD32(&s->state) == AFTER_FUNC_WAITING &&
           (!NEVERC_ATOMIC_LOAD32(&s->ready) ||
            !neverc_context_done(s->ctx)))
        Sleep(1);
    s->thread_id = GetCurrentThreadId();
    if (NEVERC_ATOMIC_CAS32(&s->state, AFTER_FUNC_WAITING,
                            AFTER_FUNC_RUNNING)) {
        s->f();
        NEVERC_ATOMIC_STORE32(&s->state, AFTER_FUNC_FINISHED);
    }
    NEVERC_ATOMIC_STORE32(&s->thread_finished, 1);
    return 0;
}
#else
#include <unistd.h>
static void *after_func_thread(void *arg) {
    after_func_state_t *s = (after_func_state_t *)arg;
    while (NEVERC_ATOMIC_LOAD32(&s->state) == AFTER_FUNC_WAITING &&
           (!NEVERC_ATOMIC_LOAD32(&s->ready) ||
            !neverc_context_done(s->ctx)))
        usleep(1000);
    s->thread_id = pthread_self();
    if (NEVERC_ATOMIC_CAS32(&s->state, AFTER_FUNC_WAITING,
                            AFTER_FUNC_RUNNING)) {
        s->f();
        NEVERC_ATOMIC_STORE32(&s->state, AFTER_FUNC_FINISHED);
    }
    NEVERC_ATOMIC_STORE32(&s->thread_finished, 1);
    return NULL;
}
#endif

#define NEVERC_AFTER_FUNC_SLOTS 4

static after_func_state_t *g_after_func_states[NEVERC_AFTER_FUNC_SLOTS];
static volatile int32_t g_after_func_lock;

static void after_func_states_lock(void) {
    while (!NEVERC_ATOMIC_CAS32(&g_after_func_lock, 0, 1)) {
        /* Registration and teardown only hold this lock briefly. */
    }
}

static void after_func_states_unlock(void) {
    NEVERC_ATOMIC_STORE32(&g_after_func_lock, 0);
}

static int after_func_slot_reclaimable(after_func_state_t *s) {
    return s &&
           NEVERC_ATOMIC_LOAD32(&s->thread_finished) &&
           NEVERC_ATOMIC_LOAD32(&s->ready) &&
           s->ctx == NULL &&
           NEVERC_ATOMIC_LOAD32(&s->state) != AFTER_FUNC_RUNNING;
}

static int after_func_stop_slot(int idx) {
    after_func_states_lock();
    after_func_state_t *s = g_after_func_states[idx];
    int stopped = s && NEVERC_ATOMIC_CAS32(&s->state, AFTER_FUNC_WAITING,
                                           AFTER_FUNC_STOPPED);
    after_func_states_unlock();
    return stopped ? 1 : 0;
}

static int after_func_runs_on_current_thread(after_func_state_t *s) {
    if (NEVERC_ATOMIC_LOAD32(&s->state) != AFTER_FUNC_RUNNING)
        return 0;
#if defined(NEVERC_PLATFORM_WINDOWS)
    return s->thread_id == GetCurrentThreadId();
#else
    return pthread_equal(s->thread_id, pthread_self()) != 0;
#endif
}

static int stop_fn_0(void)  { return after_func_stop_slot(0); }
static int stop_fn_1(void)  { return after_func_stop_slot(1); }
static int stop_fn_2(void)  { return after_func_stop_slot(2); }
static int stop_fn_3(void)  { return after_func_stop_slot(3); }

static neverc_context_stop_func_t g_stop_fns[] = {
    stop_fn_0, stop_fn_1, stop_fn_2, stop_fn_3
};

static int after_func_take_slot_locked(after_func_state_t *s) {
    for (int i = 0; i < NEVERC_AFTER_FUNC_SLOTS; i++) {
        after_func_state_t *old = g_after_func_states[i];
        if (after_func_slot_reclaimable(old)) {
            g_after_func_states[i] = NULL;
            free(old);
            old = NULL;
        }
        if (!old) {
            g_after_func_states[i] = s;
            return i;
        }
    }
    return -1;
}

static void ctx_stop_after_funcs(neverc_context_t *ctx) {
    after_func_state_t *states[NEVERC_AFTER_FUNC_SLOTS];
    int idxs[NEVERC_AFTER_FUNC_SLOTS];
    int count = 0;

    after_func_states_lock();
    for (int i = 0; i < NEVERC_AFTER_FUNC_SLOTS; i++) {
        after_func_state_t *s = g_after_func_states[i];
        if (s && s->ctx == ctx) {
            (void)NEVERC_ATOMIC_CAS32(&s->state, AFTER_FUNC_WAITING,
                                      AFTER_FUNC_STOPPED);
            states[count] = s;
            idxs[count] = i;
            count++;
        }
    }
    after_func_states_unlock();

    for (int i = 0; i < count; i++) {
        /* A callback may release its own context. The worker no longer reads
         * ctx after entering RUNNING, so waiting for that same worker would
         * only deadlock. */
        if (after_func_runs_on_current_thread(states[i]))
            continue;
        while (!NEVERC_ATOMIC_LOAD32(&states[i]->thread_finished)) {
#if defined(NEVERC_PLATFORM_WINDOWS)
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }

    after_func_states_lock();
    for (int i = 0; i < count; i++) {
        after_func_state_t *s = g_after_func_states[idxs[i]];
        if (s != states[i])
            continue;
        s->ctx = NULL;
        if (after_func_slot_reclaimable(s)) {
            g_after_func_states[idxs[i]] = NULL;
            free(s);
        }
    }
    after_func_states_unlock();
}

neverc_context_stop_func_t neverc_context_after_func(neverc_context_t *ctx,
                                                      void (*f)(void)) {
    if (!ctx || !f) return NULL;

    after_func_state_t *s = (after_func_state_t *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->ctx = ctx;
    s->f = f;

    after_func_states_lock();
    int idx = after_func_take_slot_locked(s);
    if (idx < 0) {
        after_func_states_unlock();
        free(s);
        return NULL;
    }

#if defined(NEVERC_PLATFORM_WINDOWS)
    HANDLE thread = CreateThread(NULL, 0, after_func_thread, s, 0, NULL);
    if (!thread) {
        g_after_func_states[idx] = NULL;
        after_func_states_unlock();
        free(s);
        return NULL;
    }
    CloseHandle(thread);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, after_func_thread, s) != 0) {
        g_after_func_states[idx] = NULL;
        after_func_states_unlock();
        free(s);
        return NULL;
    }
    pthread_detach(th);
#endif
    after_func_states_unlock();
    /* Publish after dropping the slot lock so f() (and nested AfterFunc /
     * context_free) cannot deadlock on g_after_func_lock. */
    NEVERC_ATOMIC_STORE32(&s->ready, 1);
    return g_stop_fns[idx];
}

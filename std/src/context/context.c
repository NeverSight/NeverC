/*
 * NeverC context — cancellation and deadline propagation.
 * Simplified C implementation of Go's context package.
 * Thread-safe using atomics.
 */

#include "neverc/std/context.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>

#if defined(NEVERC_PLATFORM_WINDOWS)
  #include <windows.h>
  static int64_t now_ms(void) {
      FILETIME ft; GetSystemTimeAsFileTime(&ft);
      uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
      return (int64_t)(t / 10000 - 11644473600000LL);
  }
#else
  #include <pthread.h>
  #include <sys/time.h>
  static int64_t now_ms(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  }
#endif

static int64_t deadline_after(int64_t timeout_ms) {
    /* Negative timeouts are already expired; INT64_MIN cannot be added. */
    if (timeout_ms < 0)
        return 1;
    int64_t now = now_ms();
    if (timeout_ms > 0 && now > INT64_MAX - timeout_ms)
        return INT64_MAX;
    int64_t deadline = now + timeout_ms;
    return deadline > 0 ? deadline : 1;
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
    volatile int32_t cancelled;
    volatile int32_t deadline_latched;
#if defined(_MSC_VER) && !defined(__clang__)
    __declspec(align(8)) volatile int64_t cancel_at;
#else
    _Alignas(8) volatile int64_t cancel_at;
#endif
    int64_t deadline_ms;
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

static void ctx_cancel_impl(neverc_context_t *ctx) {
    if (!ctx)
        return;
    int64_t ts = now_ms();
    if (ts < 1)
        ts = 1;
    /* Publish cancel_at before cancelled so a reader that observes
     * cancelled=1 always sees a non-zero timestamp. CAS64 is required on
     * 32-bit hosts where a plain 64-bit store can tear. */
    if (NEVERC_ATOMIC_CAS64(&ctx->cancel_at, (int64_t)0, ts))
        NEVERC_ATOMIC_STORE32(&ctx->cancelled, 1);
}

static int64_t ctx_load_cancel_at(const neverc_context_t *ctx) {
    return NEVERC_ATOMIC_LOAD64((int64_t *)&ctx->cancel_at);
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
    ctx_kind_t kind, neverc_context_t *parent, int64_t deadline_ms,
    neverc_context_cancel_handle_t **cancel_out) {
    if (!cancel_out)
        return NULL;
    *cancel_out = NULL;

    neverc_context_t *ctx = ctx_create(kind, parent);
    if (!ctx)
        return NULL;
    ctx->deadline_ms = deadline_ms;

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
    return ctx_with_cancel_handle(CTX_CANCEL, parent, 0, cancel_out);
}

neverc_context_t *neverc_context_with_timeout_handle(
    neverc_context_t *parent, int64_t timeout_ms,
    neverc_context_cancel_handle_t **cancel_out) {
    return ctx_with_cancel_handle(CTX_TIMEOUT, parent, deadline_after(timeout_ms),
                                  cancel_out);
}

neverc_context_t *neverc_context_with_deadline_handle(
    neverc_context_t *parent, int64_t deadline_ms,
    neverc_context_cancel_handle_t **cancel_out) {
    return ctx_with_cancel_handle(CTX_TIMEOUT, parent,
                                  deadline_ms <= 0 ? 1 : deadline_ms,
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
    ctx->deadline_ms = deadline_after(timeout_ms);
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
    /* A non-positive absolute deadline is already in the past. Store 1 so
     * done/err/cause treat it as expired (same as with_timeout(INT64_MIN)). */
    ctx->deadline_ms = deadline_ms <= 0 ? 1 : deadline_ms;
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
    ctx->deadline_ms = deadline_after(timeout_ms);
    ctx->cause = cause;
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
    ctx->deadline_ms = deadline_ms <= 0 ? 1 : deadline_ms;
    ctx->cause = cause;
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

/*
 * Go records the first cancel/deadline that fires and never overwrites Err.
 * Walk the chain and pick the earliest event so a later child deadline cannot
 * replace a parent cancel, and a later cancel() cannot replace a deadline.
 */
static const char *ctx_reason(const neverc_context_t *ctx, const char **cause_out) {
    const char *err = NULL;
    const char *cause = NULL;
    int64_t best = INT64_MAX;
    int64_t now = now_ms();

    while (ctx) {
        if (ctx->kind == CTX_WITHOUT_CANCEL)
            break;

        int64_t cancel_at = ctx_load_cancel_at(ctx);
        if (cancel_at > 0 && cancel_at < best) {
            best = cancel_at;
            err = "context canceled";
            cause = ctx->cause ? ctx->cause : err;
        }
        if (ctx->kind == CTX_TIMEOUT && ctx->deadline_ms > 0) {
            neverc_context_t *mut = (neverc_context_t *)ctx;
            int latched = NEVERC_ATOMIC_LOAD32(&mut->deadline_latched);
            if (!latched && now >= ctx->deadline_ms) {
                NEVERC_ATOMIC_STORE32(&mut->deadline_latched, 1);
                latched = 1;
            }
            if (latched && ctx->deadline_ms < best) {
                best = ctx->deadline_ms;
                err = "context deadline exceeded";
                cause = ctx->cause ? ctx->cause : err;
            }
        }
        ctx = ctx->parent;
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

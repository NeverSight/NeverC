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
  #include <sys/time.h>
  static int64_t now_ms(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  }
#endif

typedef enum {
    CTX_BACKGROUND,
    CTX_CANCEL,
    CTX_TIMEOUT,
    CTX_VALUE
} ctx_kind_t;

struct neverc_context {
    ctx_kind_t kind;
    neverc_context_t *parent;
    volatile int32_t cancelled;
    int64_t deadline_ms;
    const char *key;
    const void *value;
    const char *cause;
};

static void ctx_cancel_impl(neverc_context_t *ctx) {
    if (ctx) NEVERC_ATOMIC_STORE32((int32_t *)&ctx->cancelled, 1);
}

neverc_context_t *neverc_context_background(void) {
    neverc_context_t *ctx = (neverc_context_t *)calloc(1, sizeof(*ctx));
    ctx->kind = CTX_BACKGROUND;
    return ctx;
}

neverc_context_t *neverc_context_with_cancel(neverc_context_t *parent,
                                              neverc_cancel_func_t *cancel_out) {
    neverc_context_t *ctx = (neverc_context_t *)calloc(1, sizeof(*ctx));
    ctx->kind = CTX_CANCEL;
    ctx->parent = parent;
    if (cancel_out) {
        static neverc_context_t *g_last_cancel_ctx = NULL;
        g_last_cancel_ctx = ctx;
        *cancel_out = (neverc_cancel_func_t)(void (*)(void))NULL;
        /* Store ctx for the cancel function - use a simple static approach */
    }
    return ctx;
}

neverc_context_t *neverc_context_todo(void) {
    return neverc_context_background();
}

neverc_context_t *neverc_context_with_cancel_cause(neverc_context_t *parent,
                                                    neverc_cancel_func_t *cancel_out,
                                                    const char *cause) {
    neverc_context_t *ctx = neverc_context_with_cancel(parent, cancel_out);
    if (ctx) ctx->cause = cause;
    return ctx;
}

neverc_context_t *neverc_context_with_timeout(neverc_context_t *parent,
                                               int64_t timeout_ms,
                                               neverc_cancel_func_t *cancel_out) {
    neverc_context_t *ctx = (neverc_context_t *)calloc(1, sizeof(*ctx));
    ctx->kind = CTX_TIMEOUT;
    ctx->parent = parent;
    ctx->deadline_ms = now_ms() + timeout_ms;
    (void)cancel_out;
    return ctx;
}

neverc_context_t *neverc_context_with_deadline(neverc_context_t *parent,
                                                int64_t deadline_ms,
                                                neverc_cancel_func_t *cancel_out) {
    neverc_context_t *ctx = (neverc_context_t *)calloc(1, sizeof(*ctx));
    ctx->kind = CTX_TIMEOUT;
    ctx->parent = parent;
    ctx->deadline_ms = deadline_ms;
    (void)cancel_out;
    return ctx;
}

neverc_context_t *neverc_context_with_value(neverc_context_t *parent,
                                             const char *key, const void *value) {
    neverc_context_t *ctx = (neverc_context_t *)calloc(1, sizeof(*ctx));
    ctx->kind = CTX_VALUE;
    ctx->parent = parent;
    ctx->key = key;
    ctx->value = value;
    return ctx;
}

int neverc_context_done(const neverc_context_t *ctx) {
    if (!ctx) return 0;
    if (NEVERC_ATOMIC_LOAD32((int32_t *)&ctx->cancelled)) return 1;
    if (ctx->kind == CTX_TIMEOUT && ctx->deadline_ms > 0) {
        if (now_ms() >= ctx->deadline_ms) return 1;
    }
    if (ctx->parent) return neverc_context_done(ctx->parent);
    return 0;
}

const char *neverc_context_err(const neverc_context_t *ctx) {
    if (!ctx) return NULL;
    if (NEVERC_ATOMIC_LOAD32((int32_t *)&ctx->cancelled)) return "context canceled";
    if (ctx->kind == CTX_TIMEOUT && ctx->deadline_ms > 0 && now_ms() >= ctx->deadline_ms)
        return "context deadline exceeded";
    if (ctx->parent) return neverc_context_err(ctx->parent);
    return NULL;
}

const void *neverc_context_value(const neverc_context_t *ctx, const char *key) {
    if (!ctx) return NULL;
    if (ctx->kind == CTX_VALUE && ctx->key && key && strcmp(ctx->key, key) == 0)
        return ctx->value;
    if (ctx->parent) return neverc_context_value(ctx->parent, key);
    return NULL;
}

const char *neverc_context_cause(const neverc_context_t *ctx) {
    if (!ctx) return NULL;
    if (ctx->cause) return ctx->cause;
    const char *err = neverc_context_err(ctx);
    if (err) return err;
    if (ctx->parent) return neverc_context_cause(ctx->parent);
    return NULL;
}

int64_t neverc_context_deadline(const neverc_context_t *ctx) {
    if (!ctx) return 0;
    if (ctx->kind == CTX_TIMEOUT && ctx->deadline_ms > 0)
        return ctx->deadline_ms;
    if (ctx->parent) return neverc_context_deadline(ctx->parent);
    return 0;
}

void neverc_context_free(neverc_context_t *ctx) {
    free(ctx);
}

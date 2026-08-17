#ifndef NEVERC_CONTEXT_H
#define NEVERC_CONTEXT_H

/*
 * NeverC context — cancellation and deadline propagation.
 * Mirrors Go context package (simplified for C).
 *
 * Supports: Background, WithCancel, WithTimeout, WithValue.
 * Thread-safe via atomics.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_context neverc_context_t;
typedef struct neverc_context_cancel_handle neverc_context_cancel_handle_t;
typedef void (*neverc_cancel_func_t)(void);

neverc_context_t *neverc_context_background(void);
neverc_context_t *neverc_context_todo(void);

/* The legacy no-argument cancel callback is owned by the returned context:
 * it may be called repeatedly while ctx is alive, but is invalid after
 * neverc_context_free(ctx). At most 32 such callbacks may be live at once.
 * On callback-slot exhaustion, returns NULL and clears *cancel_out. */
neverc_context_t *neverc_context_with_cancel(neverc_context_t *parent,
                                              neverc_cancel_func_t *cancel_out);

neverc_context_t *neverc_context_with_cancel_cause(neverc_context_t *parent,
                                                    neverc_cancel_func_t *cancel_out,
                                                    const char *cause);

/* Preferred cancellation API for new code. Unlike the legacy no-argument
 * callback, explicit handles do not use a process-global trampoline slot.
 * The returned context and handle each own a reference; release both with
 * neverc_context_free() and neverc_context_cancel_handle_free(). */
neverc_context_t *neverc_context_with_cancel_handle(
    neverc_context_t *parent, neverc_context_cancel_handle_t **cancel_out);
neverc_context_t *neverc_context_with_timeout_handle(
    neverc_context_t *parent, int64_t timeout_ms,
    neverc_context_cancel_handle_t **cancel_out);
neverc_context_t *neverc_context_with_deadline_handle(
    neverc_context_t *parent, int64_t deadline_ms,
    neverc_context_cancel_handle_t **cancel_out);
void neverc_context_cancel_handle_cancel(
    neverc_context_cancel_handle_t *handle);
void neverc_context_cancel_handle_free(
    neverc_context_cancel_handle_t *handle);

neverc_context_t *neverc_context_with_timeout(neverc_context_t *parent,
                                               int64_t timeout_ms,
                                               neverc_cancel_func_t *cancel_out);

neverc_context_t *neverc_context_with_deadline(neverc_context_t *parent,
                                                int64_t deadline_ms,
                                                neverc_cancel_func_t *cancel_out);

neverc_context_t *neverc_context_with_timeout_cause(neverc_context_t *parent,
                                                     int64_t timeout_ms,
                                                     neverc_cancel_func_t *cancel_out,
                                                     const char *cause);

neverc_context_t *neverc_context_with_deadline_cause(neverc_context_t *parent,
                                                      int64_t deadline_ms,
                                                      neverc_cancel_func_t *cancel_out,
                                                      const char *cause);

neverc_context_t *neverc_context_with_value(neverc_context_t *parent,
                                             const char *key, const void *value);

neverc_context_t *neverc_context_without_cancel(neverc_context_t *parent);

int         neverc_context_done(const neverc_context_t *ctx);
const char *neverc_context_err(const neverc_context_t *ctx);
const char *neverc_context_cause(const neverc_context_t *ctx);
const void *neverc_context_value(const neverc_context_t *ctx, const char *key);
int64_t     neverc_context_deadline(const neverc_context_t *ctx);

void neverc_context_free(neverc_context_t *ctx);

typedef int (*neverc_context_stop_func_t)(void);
/* Experimental compatibility API. At most four AfterFunc registrations may
 * be in flight at once. A slot is released once its worker has finished and
 * the associated context has been freed; the stop callback is then invalid,
 * matching the legacy cancel trampoline. */
neverc_context_stop_func_t neverc_context_after_func(neverc_context_t *ctx,
                                                      void (*f)(void));

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_context_t { char __tag; };
extern struct __neverc_std_context_t __neverc_mod_context;
extern struct __neverc_std_context_t context;
#endif

#endif

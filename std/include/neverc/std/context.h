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
typedef void (*neverc_cancel_func_t)(void);

neverc_context_t *neverc_context_background(void);
neverc_context_t *neverc_context_todo(void);

neverc_context_t *neverc_context_with_cancel(neverc_context_t *parent,
                                              neverc_cancel_func_t *cancel_out);

neverc_context_t *neverc_context_with_cancel_cause(neverc_context_t *parent,
                                                    neverc_cancel_func_t *cancel_out,
                                                    const char *cause);

neverc_context_t *neverc_context_with_timeout(neverc_context_t *parent,
                                               int64_t timeout_ms,
                                               neverc_cancel_func_t *cancel_out);

neverc_context_t *neverc_context_with_deadline(neverc_context_t *parent,
                                                int64_t deadline_ms,
                                                neverc_cancel_func_t *cancel_out);

neverc_context_t *neverc_context_with_value(neverc_context_t *parent,
                                             const char *key, const void *value);

int         neverc_context_done(const neverc_context_t *ctx);
const char *neverc_context_err(const neverc_context_t *ctx);
const char *neverc_context_cause(const neverc_context_t *ctx);
const void *neverc_context_value(const neverc_context_t *ctx, const char *key);
int64_t     neverc_context_deadline(const neverc_context_t *ctx);

void neverc_context_free(neverc_context_t *ctx);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_context_t { char __tag; };
extern struct __neverc_std_context_t __neverc_mod_context;
extern struct __neverc_std_context_t context;
#endif

#endif

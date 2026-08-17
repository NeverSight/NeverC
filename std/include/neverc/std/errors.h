#ifndef NEVERC_ERRORS_H
#define NEVERC_ERRORS_H

/*
 * NeverC errors — error handling (mirrors Go errors package).
 *
 * Uses a simple error struct with string message and optional wrapped error.
 * Errors are allocated with malloc; use neverc_errors_free to release.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_error {
    const char           *msg;
    struct neverc_error  *wrapped;
    int                   owned;
} neverc_error_t;

neverc_error_t *neverc_errors_new(const char *text);
const char     *neverc_errors_message(const neverc_error_t *err);
neverc_error_t *neverc_errors_unwrap(const neverc_error_t *err);
int             neverc_errors_is(const neverc_error_t *err,
                                 const neverc_error_t *target);
/* Walk the wrap chain with the same match rules as neverc_errors_is.
 * On success, writes the matching chain node to *out when out is non-NULL. */
int             neverc_errors_as(const neverc_error_t *err,
                                 const neverc_error_t *target,
                                 neverc_error_t **out);
neverc_error_t *neverc_errors_wrap(const char *text, neverc_error_t *cause);
neverc_error_t *neverc_errors_join(neverc_error_t **errs, size_t count);
void            neverc_errors_free(neverc_error_t *err);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_errors_t { char __tag; };
extern struct __neverc_std_errors_t __neverc_mod_errors;
extern struct __neverc_std_errors_t errors;
#endif

#endif /* NEVERC_ERRORS_H */

#ifndef NEVERC_WEAK_H
#define NEVERC_WEAK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NeverC weak — weak pointers with reference counting.
 * C adaptation of Go's weak package.
 *
 * Without a garbage collector, weak pointers are implemented via a
 * shared control block that tracks both strong and weak reference
 * counts.  When the strong count drops to zero the payload is freed
 * but the control block survives until the last weak ref is released.
 *
 * Thread-safe: all ref-count operations are atomic. Retain returns an
 * independently releasable reference and fails with an empty/NULL result if
 * its count cannot be increased.
 */

typedef struct neverc_weak_ref neverc_weak_ref_t;

typedef struct {
    void *ptr;
    void *_ctrl;
} neverc_weak_strong_t;

neverc_weak_strong_t neverc_weak_new(void *data, size_t size);
neverc_weak_strong_t neverc_weak_new_with_free(void *data, void (*free_fn)(void *));

neverc_weak_strong_t neverc_weak_strong_retain(neverc_weak_strong_t s);
void                 neverc_weak_strong_release(neverc_weak_strong_t *s);

/* Requires a live strong (strong count > 0). A bitwise copy of a
 * released strong is not a live reference; use retain. */
neverc_weak_ref_t *neverc_weak_make(neverc_weak_strong_t s);
neverc_weak_ref_t *neverc_weak_ref_retain(neverc_weak_ref_t *w);
void               neverc_weak_ref_release(neverc_weak_ref_t *w);

/* Value is only a snapshot and does not keep the payload alive. Use upgrade
 * when the final strong release may happen concurrently. */
void *neverc_weak_value(neverc_weak_ref_t *w);
neverc_weak_strong_t neverc_weak_upgrade(neverc_weak_ref_t *w);

int neverc_weak_ref_equal(const neverc_weak_ref_t *a, const neverc_weak_ref_t *b);
int neverc_weak_strong_count(neverc_weak_strong_t s);
int neverc_weak_ref_count(neverc_weak_ref_t *w);

/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_weak_t { char __tag; };
extern struct __neverc_std_weak_t __neverc_mod_weak;
extern struct __neverc_std_weak_t weak;
#endif

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_WEAK_H */

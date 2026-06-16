/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_REFCOUNT_H
#define _NEVERC_KRT_LINUX_REFCOUNT_H

#include <linux/types.h>

typedef struct { atomic_t refs; } refcount_t;
#define REFCOUNT_INIT(n) { .refs = { .counter = (n) } }

static __always_inline void refcount_set(refcount_t *r, int n)
{ r->refs.counter = n; }

static __always_inline unsigned int refcount_read(const refcount_t *r)
{ return r->refs.counter; }

void refcount_inc(refcount_t *r);
bool refcount_dec_and_test(refcount_t *r);
bool refcount_inc_not_zero(refcount_t *r);

#endif

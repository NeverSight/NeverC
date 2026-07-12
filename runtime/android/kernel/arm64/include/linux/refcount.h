/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_REFCOUNT_H
#define _NEVERC_KRT_LINUX_REFCOUNT_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/compiler.h>

typedef struct { atomic_t refs; } refcount_t;
#define REFCOUNT_INIT(n) { .refs = { .counter = (n) } }

static __always_inline void refcount_set(refcount_t *r, int n)
{ r->refs.counter = n; }

static __always_inline unsigned int refcount_read(const refcount_t *r)
{ return r->refs.counter; }

/*
 * refcount_inc / dec_and_test / inc_not_zero are always inline in GKI.
 * In production (no CONFIG_REFCOUNT_FULL), they map directly to atomics.
 */
static __always_inline void refcount_inc(refcount_t *r)
{
	__atomic_add_fetch(&r->refs.counter, 1, __ATOMIC_RELAXED);
}

static __always_inline bool refcount_dec_and_test(refcount_t *r)
{
	return __atomic_sub_fetch(&r->refs.counter, 1, __ATOMIC_ACQ_REL) == 0;
}

static __always_inline bool refcount_inc_not_zero(refcount_t *r)
{
	int old = __atomic_load_n(&r->refs.counter, __ATOMIC_RELAXED);
	while (old != 0) {
		if (__atomic_compare_exchange_n(&r->refs.counter, &old,
					        old + 1, 1,
					        __ATOMIC_RELAXED,
					        __ATOMIC_RELAXED))
			return 1;
	}
	return 0;
}

#endif

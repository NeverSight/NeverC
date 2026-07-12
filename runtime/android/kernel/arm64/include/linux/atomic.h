/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_ATOMIC_H
#define _NEVERC_KRT_LINUX_ATOMIC_H

#include <linux/types.h>
#include <linux/compiler.h>

#define ATOMIC_INIT(i) { (i) }

static __always_inline int atomic_read(const atomic_t *v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);
}
static __always_inline void atomic_set(atomic_t *v, int i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);
}
static __always_inline int atomic_add_return(int i, atomic_t *v)
{
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}
static __always_inline int atomic_sub_return(int i, atomic_t *v)
{
	return __atomic_sub_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}
static __always_inline void atomic_inc(atomic_t *v) { atomic_add_return(1, v); }
static __always_inline void atomic_dec(atomic_t *v) { atomic_sub_return(1, v); }
static __always_inline int atomic_inc_return(atomic_t *v)
{
	return atomic_add_return(1, v);
}
static __always_inline int atomic_dec_return(atomic_t *v)
{
	return atomic_sub_return(1, v);
}
static __always_inline int atomic_cmpxchg(atomic_t *v, int old, int new_)
{
	__atomic_compare_exchange_n(&v->counter, &old, new_, 0,
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return old;
}

static __always_inline int atomic_xchg(atomic_t *v, int new_)
{
	return __atomic_exchange_n(&v->counter, new_, __ATOMIC_SEQ_CST);
}

static __always_inline int atomic_dec_and_test(atomic_t *v)
{
	return atomic_dec_return(v) == 0;
}

static __always_inline int atomic_inc_and_test(atomic_t *v)
{
	return atomic_inc_return(v) == 0;
}

static __always_inline int atomic_add_negative(int i, atomic_t *v)
{
	return atomic_add_return(i, v) < 0;
}

static __always_inline void atomic_add(int i, atomic_t *v)
{
	(void)atomic_add_return(i, v);
}

static __always_inline void atomic_sub(int i, atomic_t *v)
{
	(void)atomic_sub_return(i, v);
}

static __always_inline s64 atomic64_read(const atomic64_t *v)
{
	return __atomic_load_n(&v->counter, __ATOMIC_RELAXED);
}

static __always_inline void atomic64_set(atomic64_t *v, s64 i)
{
	__atomic_store_n(&v->counter, i, __ATOMIC_RELAXED);
}

static __always_inline s64 atomic64_add_return(s64 i, atomic64_t *v)
{
	return __atomic_add_fetch(&v->counter, i, __ATOMIC_SEQ_CST);
}

static __always_inline void atomic64_inc(atomic64_t *v)
{
	(void)atomic64_add_return(1, v);
}

static __always_inline void atomic64_dec(atomic64_t *v)
{
	(void)atomic64_add_return(-1, v);
}

#endif /* _NEVERC_KRT_LINUX_ATOMIC_H */

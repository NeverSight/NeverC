/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_ATOMIC_H
#define _NVK_LINUX_ATOMIC_H

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

#define smp_mb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define smp_rmb() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smp_wmb() __atomic_thread_fence(__ATOMIC_RELEASE)

#endif /* _NVK_LINUX_ATOMIC_H */

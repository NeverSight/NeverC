/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_ASM_BARRIER_H
#define _NVK_ASM_BARRIER_H

#define mb()  __asm__ __volatile__("dsb sy" : : : "memory")
#define rmb() __asm__ __volatile__("dsb ld" : : : "memory")
#define wmb() __asm__ __volatile__("dsb st" : : : "memory")
#define isb() __asm__ __volatile__("isb"    : : : "memory")

#define dmb(opt) __asm__ __volatile__("dmb " #opt : : : "memory")
#define dsb(opt) __asm__ __volatile__("dsb " #opt : : : "memory")

#define smp_mb()  dmb(ish)
#define smp_rmb() dmb(ishld)
#define smp_wmb() dmb(ishst)

#define __smp_store_release(p, v)                     \
	do {                                          \
		__asm__ __volatile__(                 \
		    "stlr %w1, [%0]"                  \
		    : : "r"(p), "r"(v) : "memory");   \
	} while (0)

#define __smp_load_acquire(p) ({                      \
	__typeof__(*(p)) __v;                         \
	__asm__ __volatile__(                         \
	    "ldar %w0, [%1]"                          \
	    : "=r"(__v) : "r"(p) : "memory");         \
	__v;                                          \
})

#define smp_store_release(p, v)                       \
	do { smp_mb(); WRITE_ONCE(*(p), (v)); } while (0)
#define smp_load_acquire(p)                           \
	({ __typeof__(*(p)) __v = READ_ONCE(*(p));    \
	   smp_mb(); __v; })

#define dma_rmb() dmb(oshld)
#define dma_wmb() dmb(oshst)

#endif /* _NVK_ASM_BARRIER_H */

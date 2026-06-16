/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_KERNEL_H
#define _NEVERC_KRT_LINUX_KERNEL_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define container_of(ptr, type, member)                                        \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define min(a, b)                                                              \
	({                                                                     \
		__typeof__(a) _a = (a);                                        \
		__typeof__(b) _b = (b);                                        \
		_a < _b ? _a : _b;                                             \
	})
#define max(a, b)                                                              \
	({                                                                     \
		__typeof__(a) _a = (a);                                        \
		__typeof__(b) _b = (b);                                        \
		_a > _b ? _a : _b;                                             \
	})
#define clamp(v, lo, hi) min(max(v, lo), hi)

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((__typeof__(x))(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((__typeof__(x))(a) - 1))
#define PTR_ALIGN(p, a) ((__typeof__(p))ALIGN((uintptr_t)(p), (a)))
#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a) - 1)) == 0)

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define round_up(x, y) ((((x) - 1) | ((y) - 1)) + 1)
#define round_down(x, y) ((x) & ~((y) - 1))

#define BIT(n) (1UL << (n))
#define BIT_ULL(n) (1ULL << (n))
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (64 - 1 - (h))))

#define swap(a, b)                                                            \
	do {                                                                  \
		__typeof__(a) __t = (a);                                       \
		(a) = (b);                                                     \
		(b) = __t;                                                     \
	} while (0)

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* _NEVERC_KRT_LINUX_KERNEL_H */

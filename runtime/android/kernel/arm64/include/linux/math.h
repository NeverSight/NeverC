/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MATH_H
#define _NEVERC_KRT_LINUX_MATH_H

#include <linux/align.h>

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_DOWN_ULL(ll, d) ({ unsigned long long _ll = (ll); (void)(((typeof(d) *)0) == ((unsigned long long *)0)); _ll / (d); })
#define DIV_ROUND_UP_ULL(ll, d) DIV_ROUND_UP((unsigned long long)(ll), (d))

#define round_up(x, y)   ((((x) - 1) | ((y) - 1)) + 1)
#define round_down(x, y) ((x) & ~((y) - 1))

#define mult_frac(x, numer, denom) ({                                         \
	typeof(x) __q = (x) / (denom);                                       \
	typeof(x) __r = (x) % (denom);                                       \
	__q * (numer) + __r * (numer) / (denom);                              \
})

#define abs(x) ({                                                             \
	typeof(x) __x = (x);                                                  \
	__x < 0 ? -__x : __x;                                                \
})

#endif /* _NEVERC_KRT_LINUX_MATH_H */

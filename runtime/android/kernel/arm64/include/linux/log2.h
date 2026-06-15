/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_LOG2_H
#define _NVK_LINUX_LOG2_H

#include <linux/types.h>
#include <linux/bitops.h>

static __always_inline int is_power_of_2(unsigned long n)
{
	return n != 0 && (n & (n - 1)) == 0;
}

static __always_inline unsigned int ilog2(unsigned long v)
{
	return fls64(v) - 1;
}

#define order_base_2(n)                                                       \
	((n) > 1 ? ilog2((n) - 1) + 1 : 0)

static __always_inline unsigned long roundup_pow_of_two(unsigned long n)
{
	if (n <= 1) return 1;
	return 1UL << (ilog2(n - 1) + 1);
}

static __always_inline unsigned long rounddown_pow_of_two(unsigned long n)
{
	return 1UL << ilog2(n);
}

#endif /* _NVK_LINUX_LOG2_H */

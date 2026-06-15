/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_BITOPS_H
#define _NVK_LINUX_BITOPS_H

#include <linux/types.h>

#define BIT(nr)          (1UL << (nr))
#define BIT_ULL(nr)      (1ULL << (nr))
#define BIT_MASK(nr)     (1UL << ((nr) % 64))
#define BIT_WORD(nr)     ((nr) / 64)
#define BITS_PER_BYTE    8
#define BITS_PER_LONG    64
#define BITS_PER_LONG_LONG 64

#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (63 - (h))))
#define GENMASK_ULL(h, l) \
	(((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (63 - (h))))

static __always_inline int fls(unsigned int x)
{
	return x ? 32 - __builtin_clz(x) : 0;
}

static __always_inline int fls64(u64 x)
{
	return x ? 64 - __builtin_clzll(x) : 0;
}

static __always_inline int ffs(int x)
{
	return __builtin_ffs(x);
}

static __always_inline unsigned long __ffs(unsigned long word)
{
	return __builtin_ctzl(word);
}

static __always_inline unsigned long __fls(unsigned long word)
{
	return 63 - __builtin_clzl(word);
}

static __always_inline int hweight32(u32 w)
{
	return __builtin_popcount(w);
}

static __always_inline int hweight64(u64 w)
{
	return __builtin_popcountll(w);
}

#define hweight_long(w) hweight64(w)

static __always_inline unsigned long
__set_bit(unsigned long nr, volatile unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] |= BIT_MASK(nr);
	return 0;
}

static __always_inline void
__clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] &= ~BIT_MASK(nr);
}

static __always_inline int
test_bit(unsigned long nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG));
}

#endif /* _NVK_LINUX_BITOPS_H */

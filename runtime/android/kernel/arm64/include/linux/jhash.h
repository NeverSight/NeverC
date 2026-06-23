/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_JHASH_H
#define _NEVERC_KRT_LINUX_JHASH_H

#include <linux/types.h>

__always_inline u32 __jhash_mix(u32 a, u32 b, u32 c) {
	a -= c; a ^= (c << 4) | (c >> 28); c += b;
	b -= a; b ^= (a << 6) | (a >> 26); a += c;
	c -= b; c ^= (b << 8) | (b >> 24); b += a;
	a -= c; a ^= (c << 16) | (c >> 16); c += b;
	b -= a; b ^= (a << 19) | (a >> 13); a += c;
	c -= b; c ^= (b << 4) | (b >> 28); b += a;
	return c;
}

__always_inline u32 jhash_1word(u32 a, u32 initval) {
	return __jhash_mix(a + 0xdeadbeef + (1 << 2) + initval, 0, 0);
}

__always_inline u32 jhash_2words(u32 a, u32 b, u32 initval) {
	return __jhash_mix(a + 0xdeadbeef + (2 << 2) + initval, b, 0);
}

__always_inline u32 jhash_3words(u32 a, u32 b, u32 c, u32 initval) {
	return __jhash_mix(a + 0xdeadbeef + (3 << 2) + initval, b, c);
}

#endif

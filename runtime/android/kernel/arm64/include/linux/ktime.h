/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_KTIME_H
#define _NEVERC_KRT_LINUX_KTIME_H

#include <linux/hrtimer.h>

enum tk_offsets {
	TK_OFFS_REAL,
	TK_OFFS_BOOT,
	TK_OFFS_TAI,
	TK_OFFS_MAX,
};

ktime_t ktime_get(void);
ktime_t ktime_get_with_offset(enum tk_offsets offs);

static __always_inline ktime_t ktime_get_real(void)
{
	return ktime_get_with_offset(TK_OFFS_REAL);
}

static __always_inline ktime_t ktime_get_boottime(void)
{
	return ktime_get_with_offset(TK_OFFS_BOOT);
}

static __always_inline s64 ktime_to_ns(ktime_t kt) { return kt; }
static __always_inline s64 ktime_to_us(ktime_t kt) { return kt / 1000; }
static __always_inline s64 ktime_to_ms(ktime_t kt) { return kt / 1000000; }

static __always_inline ktime_t ktime_add(ktime_t a, ktime_t b) { return a + b; }
static __always_inline ktime_t ktime_sub(ktime_t a, ktime_t b) { return a - b; }
static __always_inline int ktime_compare(ktime_t a, ktime_t b) {
	return a < b ? -1 : (a > b ? 1 : 0);
}
static __always_inline bool ktime_after(ktime_t a, ktime_t b) { return a > b; }
static __always_inline bool ktime_before(ktime_t a, ktime_t b) { return a < b; }

#endif

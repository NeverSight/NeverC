/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_HRTIMER_H
#define _NEVERC_KRT_LINUX_HRTIMER_H

#include <linux/types.h>

typedef s64 ktime_t;

/* Clock IDs for hrtimer_init. */
#define CLOCK_REALTIME    0
#define CLOCK_MONOTONIC   1
#define CLOCK_BOOTTIME    7

/* hrtimer modes. */
enum hrtimer_mode {
	HRTIMER_MODE_ABS      = 0x00,
	HRTIMER_MODE_REL      = 0x01,
	HRTIMER_MODE_PINNED   = 0x02,
	HRTIMER_MODE_SOFT     = 0x04,
	HRTIMER_MODE_ABS_PINNED = HRTIMER_MODE_ABS | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_REL_PINNED = HRTIMER_MODE_REL | HRTIMER_MODE_PINNED,
};

/* hrtimer restart values. */
enum hrtimer_restart {
	HRTIMER_NORESTART = 0,
	HRTIMER_RESTART   = 1,
};

struct hrtimer {
	unsigned char __opaque[128]; /* generous opaque blob */
};

typedef enum hrtimer_restart (*hrtimer_func_t)(struct hrtimer *);

void hrtimer_init(struct hrtimer *timer, int which_clock,
		  enum hrtimer_mode mode);
void hrtimer_start(struct hrtimer *timer, ktime_t time,
		   const enum hrtimer_mode mode);
int hrtimer_cancel(struct hrtimer *timer);
int hrtimer_try_to_cancel(struct hrtimer *timer);
u64 hrtimer_forward_now(struct hrtimer *timer, ktime_t interval);

/* ktime helpers. */
static __always_inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	return secs * 1000000000LL + (s64)nsecs;
}

#define ms_to_ktime(ms) ((ktime_t)(ms) * 1000000LL)
#define us_to_ktime(us) ((ktime_t)(us) * 1000LL)
#define ns_to_ktime(ns) ((ktime_t)(ns))

#endif /* _NEVERC_KRT_LINUX_HRTIMER_H */

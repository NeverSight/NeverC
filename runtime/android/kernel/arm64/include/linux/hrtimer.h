/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_HRTIMER_H
#define _NEVERC_KRT_LINUX_HRTIMER_H

#include <linux/types.h>
#include <nvkmod_version.h>

typedef s64 ktime_t;

/* Clock IDs for hrtimer_init / hrtimer_setup. */
#define CLOCK_REALTIME    0
#define CLOCK_MONOTONIC   1
#define CLOCK_BOOTTIME    7

/* hrtimer modes. */
enum hrtimer_mode {
	HRTIMER_MODE_ABS      = 0x00,
	HRTIMER_MODE_REL      = 0x01,
	HRTIMER_MODE_PINNED   = 0x02,
	HRTIMER_MODE_SOFT     = 0x04,
	HRTIMER_MODE_HARD     = 0x08,
	HRTIMER_MODE_ABS_PINNED = HRTIMER_MODE_ABS | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_REL_PINNED = HRTIMER_MODE_REL | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_ABS_HARD   = HRTIMER_MODE_ABS | HRTIMER_MODE_HARD,
	HRTIMER_MODE_REL_HARD   = HRTIMER_MODE_REL | HRTIMER_MODE_HARD,
	HRTIMER_MODE_ABS_PINNED_HARD = HRTIMER_MODE_ABS_PINNED | HRTIMER_MODE_HARD,
	HRTIMER_MODE_REL_PINNED_HARD = HRTIMER_MODE_REL_PINNED | HRTIMER_MODE_HARD,
};

/* hrtimer restart values. */
enum hrtimer_restart {
	HRTIMER_NORESTART = 0,
	HRTIMER_RESTART   = 1,
};

struct hrtimer;
typedef enum hrtimer_restart (*hrtimer_func_t)(struct hrtimer *);

/*
 * Exact opaque hrtimer storage for the official arm64 GKI configurations:
 *   timerqueue_node(32) + _softexpires(8) + function(8) + base(8) + 4×u8(4) + pad(4)
 *   5.10–6.6: + ANDROID_KABI_RESERVE(1) = u64(8)  → 72 bytes
 *   6.12–6.18: KABI_RESERVE removed                → 64 bytes
 */
struct hrtimer {
	unsigned char __before_function[40];
	hrtimer_func_t function;
#if NEVERC_KRT_KERNEL < 612
	unsigned char __after_function[24];
#else
	unsigned char __after_function[16];
#endif
};

_Static_assert(__builtin_offsetof(struct hrtimer, function) == 40,
	       "unexpected GKI hrtimer function offset");
#if NEVERC_KRT_KERNEL < 612
_Static_assert(sizeof(struct hrtimer) == 72,
	       "unexpected GKI 5.10-6.6 hrtimer layout");
#else
_Static_assert(sizeof(struct hrtimer) == 64,
	       "unexpected GKI 6.12+ hrtimer layout");
#endif

/*
 * hrtimer API evolution:
 *   5.10–6.12: hrtimer_init (exported) + manual function field assignment
 *   6.18+:     hrtimer_setup (exported) replaces hrtimer_init
 *
 *   hrtimer_start was NEVER exported (always inline → hrtimer_start_range_ns).
 *   hrtimer_forward_now was NEVER exported (accesses base->get_time()).
 *   Use hrtimer_forward (always exported) instead.
 */
#if NEVERC_KRT_KERNEL >= 618
void hrtimer_setup(struct hrtimer *timer, hrtimer_func_t function,
		   int which_clock, enum hrtimer_mode mode);
#else
void hrtimer_init(struct hrtimer *timer, int which_clock,
		  enum hrtimer_mode mode);
#endif

void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t time,
			    u64 delta_ns, const enum hrtimer_mode mode);

static __always_inline void hrtimer_start(struct hrtimer *timer, ktime_t time,
				   const enum hrtimer_mode mode)
{
	hrtimer_start_range_ns(timer, time, 0, mode);
}

int hrtimer_cancel(struct hrtimer *timer);
int hrtimer_try_to_cancel(struct hrtimer *timer);
u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval);

/* ktime helpers. */
static __always_inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	return secs * 1000000000LL + (s64)nsecs;
}

#define ms_to_ktime(ms) ((ktime_t)(ms) * 1000000LL)
#define us_to_ktime(us) ((ktime_t)(us) * 1000LL)
#define ns_to_ktime(ns) ((ktime_t)(ns))

#endif /* _NEVERC_KRT_LINUX_HRTIMER_H */

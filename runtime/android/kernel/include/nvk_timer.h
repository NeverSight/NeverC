/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TIMER_H
#define NEVERC_KRT_TIMER_H

#include <linux/types.h>
#include <linux/compiler.h>

/* ------------------------------------------------------------------ */
/*  Opaque timer/work structures sized to fit all GKI 5.10–6.12       */
/* ------------------------------------------------------------------ */

/*
 * sizeof(struct hrtimer) across GKI arm64:
 *   5.10-6.6:  72 bytes (timerqueue_node=32 + _softexpires=8 + function=8
 *                         + base=8 + state/flags=4 + pad=4
 *                         + ANDROID_KABI_RESERVE(1)=8)
 *   6.12:      64 bytes (ANDROID_KABI_RESERVE removed)
 * 128 bytes covers all with generous headroom.
 */
#define NEVERC_KRT_HRTIMER_STORAGE  128
#define NEVERC_KRT_DELAYED_WORK_STORAGE 256

struct neverc_krt_timer {
	u8  storage[NEVERC_KRT_HRTIMER_STORAGE] __attribute__((aligned(8)));
	void (*callback)(struct neverc_krt_timer *);
	int  armed;
};

struct neverc_krt_delayed_work {
	u8  storage[NEVERC_KRT_DELAYED_WORK_STORAGE] __attribute__((aligned(8)));
	void (*callback)(struct neverc_krt_delayed_work *);
	int  active;
};

/* ------------------------------------------------------------------ */
/*  Kernel function types                                             */
/* ------------------------------------------------------------------ */

int neverc_krt_timer_init(void);


/* ------------------------------------------------------------------ */
/*  High-resolution timer API                                         */
/* ------------------------------------------------------------------ */

#define NEVERC_KRT_CLOCK_MONOTONIC 1
#define NEVERC_KRT_HRTIMER_ABS     0
#define NEVERC_KRT_HRTIMER_REL     1

/*
 * After hrtimer_init, locate the .function field by finding the
 * .base kernel pointer (first non-zero pointer after offset 16)
 * and writing one slot before it.  function is at offset 40 on
 * all GKI 5.10-6.12 (rb_node=24 + expires=8 + _softexpires=8).
 */


int neverc_krt_timer_setup(struct neverc_krt_timer *t,
			   void (*cb)(struct neverc_krt_timer *));


int neverc_krt_timer_start_ns(struct neverc_krt_timer *t, s64 nsecs);


int neverc_krt_timer_start_ms(struct neverc_krt_timer *t, unsigned int ms);


int neverc_krt_timer_start_us(struct neverc_krt_timer *t, unsigned int us);


int neverc_krt_timer_cancel(struct neverc_krt_timer *t);



/* ------------------------------------------------------------------ */
/*  Timestamp utilities                                               */
/* ------------------------------------------------------------------ */

u64 neverc_krt_ktime_get_ns(void);


u64 neverc_krt_ktime_get_boot_ns(void);


static __always_inline u64 neverc_krt_arch_counter(void)
{
	u64 cnt;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(cnt));
	return cnt;
}

static __always_inline u32 neverc_krt_arch_counter_freq(void)
{
	u64 freq;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	return (u32)freq;
}

static __always_inline u64 neverc_krt_arch_counter_to_ns(u64 ticks)
{
	u32 freq = neverc_krt_arch_counter_freq();
	if (!freq) return 0;
	return ticks * 1000000000ULL / freq;
}

/* Simple busy-wait delay (microseconds). Use only for very short waits. */
void neverc_krt_udelay(unsigned int us);


#endif /* NEVERC_KRT_TIMER_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TIMER_H
#define NEVERC_KRT_TIMER_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

/* ------------------------------------------------------------------ */
/*  Opaque timer/work structures sized to fit all GKI 5.10–6.12       */
/* ------------------------------------------------------------------ */

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

typedef void (*neverc_krt_hrt_init_fn)(void *timer, int clock_id, int mode);
typedef int  (*neverc_krt_hrt_start_fn)(void *timer, s64 tim, int mode);
typedef int  (*neverc_krt_hrt_cancel_fn)(void *timer);
typedef s64  (*neverc_krt_ktime_set_fn)(long secs, long nsecs);

typedef void (*neverc_krt_init_work_fn)(void *work, void *func);
typedef int  (*neverc_krt_schedule_dw_fn)(void *dwork, unsigned long delay);
typedef int  (*neverc_krt_cancel_dw_fn)(void *dwork);
typedef unsigned long (*neverc_krt_msecs_to_jiffies_fn)(unsigned int m);

NEVERC_KRT_RT_VAR neverc_krt_hrt_init_fn      _neverc_krt_hrtimer_init;
NEVERC_KRT_RT_VAR neverc_krt_hrt_start_fn     _neverc_krt_hrtimer_start;
NEVERC_KRT_RT_VAR neverc_krt_hrt_cancel_fn    _neverc_krt_hrtimer_cancel;
NEVERC_KRT_RT_VAR neverc_krt_init_work_fn     _neverc_krt_init_delayed_work;
NEVERC_KRT_RT_VAR neverc_krt_schedule_dw_fn   _neverc_krt_schedule_delayed_work;
NEVERC_KRT_RT_VAR neverc_krt_cancel_dw_fn     _neverc_krt_cancel_delayed_work;
NEVERC_KRT_RT_VAR neverc_krt_msecs_to_jiffies_fn _neverc_krt_msecs_to_jiffies;
NEVERC_KRT_RT_VAR int                  _neverc_krt_timer_inited;

/* hrtimer callback wrapper: the kernel passes hrtimer*, we extract neverc_krt_timer* */
static __always_inline struct neverc_krt_timer *
_neverc_krt_timer_from_storage(void *hrt)
{
	return (struct neverc_krt_timer *)((char *)hrt -
		__builtin_offsetof(struct neverc_krt_timer, storage));
}

int _neverc_krt_hrt_trampoline(void *hrt);


int _neverc_krt_hrt_trampoline_repeat(void *hrt);


int neverc_krt_timer_init(void);


/* ------------------------------------------------------------------ */
/*  High-resolution timer API                                         */
/* ------------------------------------------------------------------ */

#define NEVERC_KRT_CLOCK_MONOTONIC 1
#define NEVERC_KRT_HRTIMER_ABS     0
#define NEVERC_KRT_HRTIMER_REL     1

/*
 * After hrtimer_init, the .function field (a pointer) is NULL.
 * We scan the opaque storage starting at offset 16 (past the
 * spinlock/rb_node) for the first NULL pointer slot and patch it.
 * On GKI 5.10 it's at 24, on 6.1+ it may be 32 or 40.
 */
int _neverc_krt_hrt_patch_fn(u8 *storage, unsigned long fn);


int neverc_krt_timer_setup(struct neverc_krt_timer *t,
			   void (*cb)(struct neverc_krt_timer *));


int neverc_krt_timer_start_ns(struct neverc_krt_timer *t, s64 nsecs);


int neverc_krt_timer_start_ms(struct neverc_krt_timer *t, unsigned int ms);


int neverc_krt_timer_start_us(struct neverc_krt_timer *t, unsigned int us);


int neverc_krt_timer_cancel(struct neverc_krt_timer *t);



/* ------------------------------------------------------------------ */
/*  Timestamp utilities                                               */
/* ------------------------------------------------------------------ */

typedef u64 (*neverc_krt_ktime_get_fn)(void);
NEVERC_KRT_RT_VAR neverc_krt_ktime_get_fn _neverc_krt_ktime_get;
NEVERC_KRT_RT_VAR neverc_krt_ktime_get_fn _neverc_krt_ktime_get_boot;

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
	u32 freq;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	return freq;
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

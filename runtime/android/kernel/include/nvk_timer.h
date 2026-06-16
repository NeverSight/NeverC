/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_TIMER_H
#define NVK_TIMER_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

/* ------------------------------------------------------------------ */
/*  Opaque timer/work structures sized to fit all GKI 5.10–6.12       */
/* ------------------------------------------------------------------ */

#define NVK_HRTIMER_STORAGE  128
#define NVK_DELAYED_WORK_STORAGE 256

struct nvk_timer {
	u8  storage[NVK_HRTIMER_STORAGE] __attribute__((aligned(8)));
	void (*callback)(struct nvk_timer *);
	int  armed;
};

struct nvk_delayed_work {
	u8  storage[NVK_DELAYED_WORK_STORAGE] __attribute__((aligned(8)));
	void (*callback)(struct nvk_delayed_work *);
	int  active;
};

/* ------------------------------------------------------------------ */
/*  Kernel function types                                             */
/* ------------------------------------------------------------------ */

typedef void (*nvk_hrt_init_fn)(void *timer, int clock_id, int mode);
typedef int  (*nvk_hrt_start_fn)(void *timer, s64 tim, int mode);
typedef int  (*nvk_hrt_cancel_fn)(void *timer);
typedef s64  (*nvk_ktime_set_fn)(long secs, long nsecs);

typedef void (*nvk_init_work_fn)(void *work, void *func);
typedef int  (*nvk_schedule_dw_fn)(void *dwork, unsigned long delay);
typedef int  (*nvk_cancel_dw_fn)(void *dwork);
typedef unsigned long (*nvk_msecs_to_jiffies_fn)(unsigned int m);

NVK_RT_VAR nvk_hrt_init_fn      _nvk_hrtimer_init;
NVK_RT_VAR nvk_hrt_start_fn     _nvk_hrtimer_start;
NVK_RT_VAR nvk_hrt_cancel_fn    _nvk_hrtimer_cancel;
NVK_RT_VAR nvk_init_work_fn     _nvk_init_delayed_work;
NVK_RT_VAR nvk_schedule_dw_fn   _nvk_schedule_delayed_work;
NVK_RT_VAR nvk_cancel_dw_fn     _nvk_cancel_delayed_work;
NVK_RT_VAR nvk_msecs_to_jiffies_fn _nvk_msecs_to_jiffies;
NVK_RT_VAR int                  _nvk_timer_inited;

/* hrtimer callback wrapper: the kernel passes hrtimer*, we extract nvk_timer* */
static __always_inline struct nvk_timer *
_nvk_timer_from_storage(void *hrt)
{
	return (struct nvk_timer *)((char *)hrt -
		__builtin_offsetof(struct nvk_timer, storage));
}

int _nvk_hrt_trampoline(void *hrt);


int _nvk_hrt_trampoline_repeat(void *hrt);


int nvk_timer_init(void);


/* ------------------------------------------------------------------ */
/*  High-resolution timer API                                         */
/* ------------------------------------------------------------------ */

#define NVK_CLOCK_MONOTONIC 1
#define NVK_HRTIMER_ABS     0
#define NVK_HRTIMER_REL     1

/*
 * After hrtimer_init, the .function field (a pointer) is NULL.
 * We scan the opaque storage starting at offset 16 (past the
 * spinlock/rb_node) for the first NULL pointer slot and patch it.
 * On GKI 5.10 it's at 24, on 6.1+ it may be 32 or 40.
 */
int _nvk_hrt_patch_fn(u8 *storage, unsigned long fn);


int nvk_timer_setup(struct nvk_timer *t,
			   void (*cb)(struct nvk_timer *));


int nvk_timer_start_ns(struct nvk_timer *t, s64 nsecs);


int nvk_timer_start_ms(struct nvk_timer *t, unsigned int ms);


int nvk_timer_start_us(struct nvk_timer *t, unsigned int us);


int nvk_timer_cancel(struct nvk_timer *t);



/* ------------------------------------------------------------------ */
/*  Timestamp utilities                                               */
/* ------------------------------------------------------------------ */

typedef u64 (*nvk_ktime_get_fn)(void);
NVK_RT_VAR nvk_ktime_get_fn _nvk_ktime_get;
NVK_RT_VAR nvk_ktime_get_fn _nvk_ktime_get_boot;

u64 nvk_ktime_get_ns(void);


u64 nvk_ktime_get_boot_ns(void);


static __always_inline u64 nvk_arch_counter(void)
{
	u64 cnt;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(cnt));
	return cnt;
}

static __always_inline u32 nvk_arch_counter_freq(void)
{
	u32 freq;
	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
	return freq;
}

static __always_inline u64 nvk_arch_counter_to_ns(u64 ticks)
{
	u32 freq = nvk_arch_counter_freq();
	if (!freq) return 0;
	return ticks * 1000000000ULL / freq;
}

/* Simple busy-wait delay (microseconds). Use only for very short waits. */
void nvk_udelay(unsigned int us);


#endif /* NVK_TIMER_H */

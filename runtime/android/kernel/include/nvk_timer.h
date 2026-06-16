/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_TIMER_H
#define NVK_TIMER_H

#include <linux/types.h>
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

static nvk_hrt_init_fn      _nvk_hrtimer_init;
static nvk_hrt_start_fn     _nvk_hrtimer_start;
static nvk_hrt_cancel_fn    _nvk_hrtimer_cancel;
static nvk_init_work_fn     _nvk_init_delayed_work;
static nvk_schedule_dw_fn   _nvk_schedule_delayed_work;
static nvk_cancel_dw_fn     _nvk_cancel_delayed_work;
static nvk_msecs_to_jiffies_fn _nvk_msecs_to_jiffies;
static int                  _nvk_timer_inited;

/* hrtimer callback wrapper: the kernel passes hrtimer*, we extract nvk_timer* */
static __always_inline struct nvk_timer *
_nvk_timer_from_storage(void *hrt)
{
	return (struct nvk_timer *)((char *)hrt -
		__builtin_offsetof(struct nvk_timer, storage));
}

static int _nvk_hrt_trampoline(void *hrt)
{
	struct nvk_timer *t = _nvk_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 0;  /* HRTIMER_NORESTART */
}

static int _nvk_hrt_trampoline_repeat(void *hrt)
{
	struct nvk_timer *t = _nvk_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 1;  /* HRTIMER_RESTART */
}

static int nvk_timer_init(void)
{
	if (_nvk_timer_inited) return 0;

	_nvk_hrtimer_init   = (nvk_hrt_init_fn)NVK_LOOKUP("hrtimer_init");
	_nvk_hrtimer_start  = (nvk_hrt_start_fn)NVK_LOOKUP("hrtimer_start");
	if (!_nvk_hrtimer_start)
		_nvk_hrtimer_start =
			(nvk_hrt_start_fn)NVK_LOOKUP("hrtimer_start_range_ns");
	_nvk_hrtimer_cancel = (nvk_hrt_cancel_fn)NVK_LOOKUP("hrtimer_cancel");

	_nvk_init_delayed_work =
		(nvk_init_work_fn)NVK_LOOKUP("__init_work");
	_nvk_schedule_delayed_work =
		(nvk_schedule_dw_fn)NVK_LOOKUP("schedule_delayed_work");
	if (!_nvk_schedule_delayed_work)
		_nvk_schedule_delayed_work =
			(nvk_schedule_dw_fn)NVK_LOOKUP("queue_delayed_work_on");
	_nvk_cancel_delayed_work =
		(nvk_cancel_dw_fn)NVK_LOOKUP("cancel_delayed_work_sync");

	_nvk_msecs_to_jiffies =
		(nvk_msecs_to_jiffies_fn)NVK_LOOKUP("__msecs_to_jiffies");
	if (!_nvk_msecs_to_jiffies)
		_nvk_msecs_to_jiffies =
			(nvk_msecs_to_jiffies_fn)NVK_LOOKUP("msecs_to_jiffies");

	_nvk_ktime_get = (nvk_ktime_get_fn)NVK_LOOKUP("ktime_get");
	if (!_nvk_ktime_get)
		_nvk_ktime_get =
			(nvk_ktime_get_fn)NVK_LOOKUP("ktime_get_mono_fast_ns");
	_nvk_ktime_get_boot =
		(nvk_ktime_get_fn)NVK_LOOKUP("ktime_get_boottime");
	if (!_nvk_ktime_get_boot)
		_nvk_ktime_get_boot =
			(nvk_ktime_get_fn)NVK_LOOKUP("ktime_get_boot_fast_ns");

	_nvk_timer_inited = 1;
	return 0;
}

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
static int _nvk_hrt_patch_fn(u8 *storage, unsigned long fn)
{
	int off;
	for (off = 16; off <= 64; off += 8) {
		unsigned long *slot = (unsigned long *)(storage + off);
		if (*slot == 0) {
			*slot = fn;
			return 0;
		}
	}
	/* Fallback: try the known 5.10 offset */
	*(unsigned long *)(storage + 24) = fn;
	return 0;
}

static int nvk_timer_setup(struct nvk_timer *t,
			   void (*cb)(struct nvk_timer *))
{
	if (!t || !cb) return -1;
	if (!_nvk_hrtimer_init) return -2;
	__builtin_memset(t, 0, sizeof(*t));
	t->callback = cb;
	_nvk_hrtimer_init(t->storage, NVK_CLOCK_MONOTONIC, NVK_HRTIMER_REL);
	_nvk_hrt_patch_fn(t->storage, (unsigned long)_nvk_hrt_trampoline);
	t->armed = 0;
	return 0;
}

static int nvk_timer_start_ns(struct nvk_timer *t, s64 nsecs)
{
	if (!t || !_nvk_hrtimer_start) return -1;
	t->armed = 1;
	return _nvk_hrtimer_start(t->storage, nsecs, NVK_HRTIMER_REL);
}

static int nvk_timer_start_ms(struct nvk_timer *t, unsigned int ms)
{
	return nvk_timer_start_ns(t, (s64)ms * 1000000LL);
}

static int nvk_timer_start_us(struct nvk_timer *t, unsigned int us)
{
	return nvk_timer_start_ns(t, (s64)us * 1000LL);
}

static int nvk_timer_cancel(struct nvk_timer *t)
{
	if (!t || !_nvk_hrtimer_cancel) return -1;
	t->armed = 0;
	return _nvk_hrtimer_cancel(t->storage);
}


/* ------------------------------------------------------------------ */
/*  Timestamp utilities                                               */
/* ------------------------------------------------------------------ */

typedef u64 (*nvk_ktime_get_fn)(void);
static nvk_ktime_get_fn _nvk_ktime_get;
static nvk_ktime_get_fn _nvk_ktime_get_boot;

static u64 nvk_ktime_get_ns(void)
{
	return _nvk_ktime_get ? _nvk_ktime_get() : 0;
}

static u64 nvk_ktime_get_boot_ns(void)
{
	return _nvk_ktime_get_boot ? _nvk_ktime_get_boot() : 0;
}

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
static void nvk_udelay(unsigned int us)
{
	u64 start = nvk_arch_counter();
	u32 freq = nvk_arch_counter_freq();
	u64 target = (u64)us * freq / 1000000ULL;
	while (nvk_arch_counter() - start < target)
		__asm__ __volatile__("yield" ::: "memory");
}

#endif /* NVK_TIMER_H */

/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

/* ---- internal inline helpers ---- */

static __always_inline struct neverc_krt_timer *
_neverc_krt_timer_from_storage(void *hrt)
{
	return (struct neverc_krt_timer *)((char *)hrt -
		__builtin_offsetof(struct neverc_krt_timer, storage));
}

/* ---- internal typedefs ---- */

typedef void (*neverc_krt_hrt_init_fn)(void *timer, int clock_id, int mode);
typedef int  (*neverc_krt_hrt_start_fn)(void *timer, s64 tim, int mode);
typedef int  (*neverc_krt_hrt_cancel_fn)(void *timer);
typedef s64  (*neverc_krt_ktime_set_fn)(long secs, long nsecs);
typedef void (*neverc_krt_init_work_fn)(void *work, void *func);
typedef int  (*neverc_krt_schedule_dw_fn)(void *dwork, unsigned long delay);
typedef int  (*neverc_krt_cancel_dw_fn)(void *dwork);
typedef unsigned long (*neverc_krt_msecs_to_jiffies_fn)(unsigned int m);
typedef u64  (*neverc_krt_ktime_get_fn)(void);

static neverc_krt_hrt_init_fn        _neverc_krt_hrtimer_init;
static neverc_krt_hrt_start_fn       _neverc_krt_hrtimer_start;
static neverc_krt_hrt_cancel_fn      _neverc_krt_hrtimer_cancel;
static neverc_krt_init_work_fn       _neverc_krt_init_delayed_work;
static neverc_krt_schedule_dw_fn     _neverc_krt_schedule_delayed_work;
static neverc_krt_cancel_dw_fn       _neverc_krt_cancel_delayed_work;
static neverc_krt_msecs_to_jiffies_fn _neverc_krt_msecs_to_jiffies;
static int                           _neverc_krt_timer_inited;
static neverc_krt_ktime_get_fn       _neverc_krt_ktime_get;
static neverc_krt_ktime_get_fn       _neverc_krt_ktime_get_boot;

static int _neverc_krt_hrt_trampoline(void *hrt)
{
	struct neverc_krt_timer *t = _neverc_krt_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 0;  /* HRTIMER_NORESTART */
}

static int _neverc_krt_hrt_trampoline_repeat(void *hrt)
{
	struct neverc_krt_timer *t = _neverc_krt_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 1;  /* HRTIMER_RESTART */
}

int neverc_krt_timer_init(void)
{
	if (_neverc_krt_timer_inited) return 0;

	_neverc_krt_hrtimer_init   = (neverc_krt_hrt_init_fn)NEVERC_KRT_LOOKUP("hrtimer_init");
	_neverc_krt_hrtimer_start  = (neverc_krt_hrt_start_fn)NEVERC_KRT_LOOKUP("hrtimer_start");
	if (!_neverc_krt_hrtimer_start)
		_neverc_krt_hrtimer_start =
			(neverc_krt_hrt_start_fn)NEVERC_KRT_LOOKUP("hrtimer_start_range_ns");
	_neverc_krt_hrtimer_cancel = (neverc_krt_hrt_cancel_fn)NEVERC_KRT_LOOKUP("hrtimer_cancel");

	_neverc_krt_init_delayed_work =
		(neverc_krt_init_work_fn)NEVERC_KRT_LOOKUP("__init_work");
	_neverc_krt_schedule_delayed_work =
		(neverc_krt_schedule_dw_fn)NEVERC_KRT_LOOKUP("schedule_delayed_work");
	if (!_neverc_krt_schedule_delayed_work)
		_neverc_krt_schedule_delayed_work =
			(neverc_krt_schedule_dw_fn)NEVERC_KRT_LOOKUP("queue_delayed_work_on");
	_neverc_krt_cancel_delayed_work =
		(neverc_krt_cancel_dw_fn)NEVERC_KRT_LOOKUP("cancel_delayed_work_sync");

	_neverc_krt_msecs_to_jiffies =
		(neverc_krt_msecs_to_jiffies_fn)NEVERC_KRT_LOOKUP("__msecs_to_jiffies");
	if (!_neverc_krt_msecs_to_jiffies)
		_neverc_krt_msecs_to_jiffies =
			(neverc_krt_msecs_to_jiffies_fn)NEVERC_KRT_LOOKUP("msecs_to_jiffies");

	_neverc_krt_ktime_get = (neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get");
	if (!_neverc_krt_ktime_get)
		_neverc_krt_ktime_get =
			(neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get_mono_fast_ns");
	_neverc_krt_ktime_get_boot =
		(neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get_boottime");
	if (!_neverc_krt_ktime_get_boot)
		_neverc_krt_ktime_get_boot =
			(neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get_boot_fast_ns");

	_neverc_krt_timer_inited = 1;
	return 0;
}

static int _neverc_krt_hrt_patch_fn(u8 *storage, unsigned long fn)
{
	/*
	 * After hrtimer_init (which does memset(0) + sets base pointer):
	 *   [0]  __rb_parent_color  (self-pointer, non-zero)
	 *   [8]  rb_right           (0)
	 *   [16] rb_left            (0)
	 *   [24] expires            (0)
	 *   [32] _softexpires       (0)
	 *   [40] function           (0)   ← target
	 *   [48] base               (kernel ptr, non-zero) ← landmark
	 *
	 * Find `base` (first kernel pointer after offset 16), then write
	 * fn one slot before it.  Stable across GKI 5.10-6.12.
	 */
	int off;
	for (off = 24; off <= 96; off += 8) {
		unsigned long val;
		if (neverc_krt_mem_read(&val, storage + off, 8))
			continue;
		if (val > 0xFFFF000000000000UL &&
		    val < 0xFFFFFFFFFFFFF000UL)
			return neverc_krt_mem_write(storage + off - 8, &fn, 8);
	}
	return neverc_krt_mem_write(storage + 40, &fn, 8);
}

int neverc_krt_timer_setup(struct neverc_krt_timer *t,
			   void (*cb)(struct neverc_krt_timer *))
{
	if (!t || !cb) return -1;
	if (!_neverc_krt_hrtimer_init) return -2;
	__builtin_memset(t, 0, sizeof(*t));
	t->callback = cb;
	_neverc_krt_hrtimer_init(t->storage, NEVERC_KRT_CLOCK_MONOTONIC, NEVERC_KRT_HRTIMER_REL);
	_neverc_krt_hrt_patch_fn(t->storage, (unsigned long)_neverc_krt_hrt_trampoline);
	t->armed = 0;
	return 0;
}

int neverc_krt_timer_start_ns(struct neverc_krt_timer *t, s64 nsecs)
{
	if (!t || !_neverc_krt_hrtimer_start) return -1;
	t->armed = 1;
	return _neverc_krt_hrtimer_start(t->storage, nsecs, NEVERC_KRT_HRTIMER_REL);
}

int neverc_krt_timer_start_ms(struct neverc_krt_timer *t, unsigned int ms)
{
	return neverc_krt_timer_start_ns(t, (s64)ms * 1000000LL);
}

int neverc_krt_timer_start_us(struct neverc_krt_timer *t, unsigned int us)
{
	return neverc_krt_timer_start_ns(t, (s64)us * 1000LL);
}

int neverc_krt_timer_cancel(struct neverc_krt_timer *t)
{
	if (!t || !_neverc_krt_hrtimer_cancel) return -1;
	t->armed = 0;
	return _neverc_krt_hrtimer_cancel(t->storage);
}

u64 neverc_krt_ktime_get_ns(void)
{
	return _neverc_krt_ktime_get ? _neverc_krt_ktime_get() : 0;
}

u64 neverc_krt_ktime_get_boot_ns(void)
{
	return _neverc_krt_ktime_get_boot ? _neverc_krt_ktime_get_boot() : 0;
}

void neverc_krt_udelay(unsigned int us)
{
	u64 start = neverc_krt_arch_counter();
	u32 freq = neverc_krt_arch_counter_freq();
	u64 target = (u64)us * freq / 1000000ULL;
	while (neverc_krt_arch_counter() - start < target)
		__asm__ __volatile__("yield" ::: "memory");
}


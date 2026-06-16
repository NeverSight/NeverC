/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_timer.c — implementations extracted from neverc_krt_timer.h. */
#include <nvk.h>

int _neverc_krt_hrt_trampoline(void *hrt)
{
	struct neverc_krt_timer *t = _neverc_krt_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 0;  /* HRTIMER_NORESTART */
}

int _neverc_krt_hrt_trampoline_repeat(void *hrt)
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

int _neverc_krt_hrt_patch_fn(u8 *storage, unsigned long fn)
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


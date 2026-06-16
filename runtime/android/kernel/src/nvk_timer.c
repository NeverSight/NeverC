/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_timer.c — implementations extracted from nvk_timer.h. */
#include <nvk.h>

int _nvk_hrt_trampoline(void *hrt)
{
	struct nvk_timer *t = _nvk_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 0;  /* HRTIMER_NORESTART */
}

int _nvk_hrt_trampoline_repeat(void *hrt)
{
	struct nvk_timer *t = _nvk_timer_from_storage(hrt);
	if (t->callback)
		t->callback(t);
	return 1;  /* HRTIMER_RESTART */
}

int nvk_timer_init(void)
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

int _nvk_hrt_patch_fn(u8 *storage, unsigned long fn)
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

int nvk_timer_setup(struct nvk_timer *t,
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

int nvk_timer_start_ns(struct nvk_timer *t, s64 nsecs)
{
	if (!t || !_nvk_hrtimer_start) return -1;
	t->armed = 1;
	return _nvk_hrtimer_start(t->storage, nsecs, NVK_HRTIMER_REL);
}

int nvk_timer_start_ms(struct nvk_timer *t, unsigned int ms)
{
	return nvk_timer_start_ns(t, (s64)ms * 1000000LL);
}

int nvk_timer_start_us(struct nvk_timer *t, unsigned int us)
{
	return nvk_timer_start_ns(t, (s64)us * 1000LL);
}

int nvk_timer_cancel(struct nvk_timer *t)
{
	if (!t || !_nvk_hrtimer_cancel) return -1;
	t->armed = 0;
	return _nvk_hrtimer_cancel(t->storage);
}

u64 nvk_ktime_get_ns(void)
{
	return _nvk_ktime_get ? _nvk_ktime_get() : 0;
}

u64 nvk_ktime_get_boot_ns(void)
{
	return _nvk_ktime_get_boot ? _nvk_ktime_get_boot() : 0;
}

void nvk_udelay(unsigned int us)
{
	u64 start = nvk_arch_counter();
	u32 freq = nvk_arch_counter_freq();
	u64 target = (u64)us * freq / 1000000ULL;
	while (nvk_arch_counter() - start < target)
		__asm__ __volatile__("yield" ::: "memory");
}


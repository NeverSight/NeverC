/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include <linux/hrtimer.h>

#define NEVERC_KRT_TIMER_FORCE_INLINE __attribute__((always_inline))

/* ---- internal inline helpers ---- */

static __always_inline struct neverc_krt_timer *
_neverc_krt_timer_from_storage(void *hrt)
{
	return (struct neverc_krt_timer *)((char *)hrt -
		__builtin_offsetof(struct neverc_krt_timer, storage));
}

/* ---- internal typedefs ---- */

typedef void (*neverc_krt_hrt_init_fn)(struct hrtimer *timer, int clock_id,
				       enum hrtimer_mode mode);
typedef void (*neverc_krt_hrt_setup_fn)(struct hrtimer *timer,
					hrtimer_func_t function,
					int clock_id,
					enum hrtimer_mode mode);
typedef void (*neverc_krt_hrt_start_range_fn)(struct hrtimer *timer,
					      ktime_t tim, u64 delta_ns,
					      enum hrtimer_mode mode);
typedef int  (*neverc_krt_hrt_cancel_fn)(struct hrtimer *timer);
typedef u64  (*neverc_krt_ktime_get_fn)(void);
typedef s64  (*neverc_krt_ktime_get_offset_fn)(int offs);

static neverc_krt_hrt_init_fn        _neverc_krt_hrtimer_init;
static neverc_krt_hrt_setup_fn       _neverc_krt_hrtimer_setup;
static neverc_krt_hrt_start_range_fn _neverc_krt_hrtimer_start_range;
static neverc_krt_hrt_cancel_fn      _neverc_krt_hrtimer_cancel;
static int                           _neverc_krt_timer_inited;
static neverc_krt_ktime_get_fn       _neverc_krt_ktime_get;
static neverc_krt_ktime_get_fn       _neverc_krt_ktime_get_boot;
static neverc_krt_ktime_get_offset_fn _neverc_krt_ktime_get_offset;

static enum hrtimer_restart
_neverc_krt_hrt_trampoline(struct hrtimer *hrt)
{
	struct neverc_krt_timer *t = _neverc_krt_timer_from_storage(hrt);
	t->armed = 0;
	if (t->callback)
		t->callback(t);
	return HRTIMER_NORESTART;
}

NEVERC_KRT_TIMER_FORCE_INLINE u64 neverc_krt_arch_counter(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(value));
	return value;
}

NEVERC_KRT_TIMER_FORCE_INLINE u32 neverc_krt_arch_counter_freq(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(value));
	return (u32)value;
}

int neverc_krt_timer_init(void)
{
	if (_neverc_krt_timer_inited) return 0;

	/*
	 * 6.18+ replaced hrtimer_init with hrtimer_setup(timer, fn, clk, mode)
	 * and hrtimer_start with hrtimer_start_range_ns(timer, tim, delta, mode).
	 * Try the new symbols first, fall back to the old ones.
	 */
	_neverc_krt_hrtimer_setup = (neverc_krt_hrt_setup_fn)NEVERC_KRT_LOOKUP("hrtimer_setup");
	if (!_neverc_krt_hrtimer_setup)
		_neverc_krt_hrtimer_init = (neverc_krt_hrt_init_fn)NEVERC_KRT_LOOKUP("hrtimer_init");
	_neverc_krt_hrtimer_start_range =
		(neverc_krt_hrt_start_range_fn)NEVERC_KRT_LOOKUP("hrtimer_start_range_ns");
	_neverc_krt_hrtimer_cancel = (neverc_krt_hrt_cancel_fn)NEVERC_KRT_LOOKUP("hrtimer_cancel");

	_neverc_krt_ktime_get = (neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get");
	if (!_neverc_krt_ktime_get)
		_neverc_krt_ktime_get =
			(neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get_mono_fast_ns");
	_neverc_krt_ktime_get_boot =
		(neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get_boottime");
	if (!_neverc_krt_ktime_get_boot)
		_neverc_krt_ktime_get_boot =
			(neverc_krt_ktime_get_fn)NEVERC_KRT_LOOKUP("ktime_get_boot_fast_ns");
	if (!_neverc_krt_ktime_get_boot)
		_neverc_krt_ktime_get_offset =
			(neverc_krt_ktime_get_offset_fn)NEVERC_KRT_LOOKUP("ktime_get_with_offset");

	if ((!_neverc_krt_hrtimer_init && !_neverc_krt_hrtimer_setup) ||
	    !_neverc_krt_hrtimer_start_range || !_neverc_krt_hrtimer_cancel)
		return -1;

	_neverc_krt_timer_inited = 1;
	return 0;
}

int neverc_krt_timer_setup(struct neverc_krt_timer *t,
			   void (*cb)(struct neverc_krt_timer *))
{
	if (!t || !cb) return -1;
	if (!_neverc_krt_hrtimer_init && !_neverc_krt_hrtimer_setup)
		return -2;
	__builtin_memset(t, 0, sizeof(*t));
	t->callback = cb;
	if (_neverc_krt_hrtimer_setup) {
		_neverc_krt_hrtimer_setup((struct hrtimer *)t->storage,
					  _neverc_krt_hrt_trampoline,
					  NEVERC_KRT_CLOCK_MONOTONIC,
					  NEVERC_KRT_HRTIMER_REL);
	} else {
		struct hrtimer *timer = (struct hrtimer *)t->storage;
		_neverc_krt_hrtimer_init(timer,
					 NEVERC_KRT_CLOCK_MONOTONIC,
					 NEVERC_KRT_HRTIMER_REL);
		timer->function = _neverc_krt_hrt_trampoline;
	}
	t->armed = 0;
	return 0;
}

int neverc_krt_timer_start_ns(struct neverc_krt_timer *t, s64 nsecs)
{
	if (!t || !_neverc_krt_hrtimer_start_range) return -1;
	t->armed = 1;
	_neverc_krt_hrtimer_start_range((struct hrtimer *)t->storage, nsecs,
					0, NEVERC_KRT_HRTIMER_REL);
	return 0;
}

int neverc_krt_timer_start_ms(struct neverc_krt_timer *t, unsigned int ms)
{
	return neverc_krt_timer_start_ns(
		t, (s64)ms * NEVERC_KRT_NSEC_PER_MSEC);
}

int neverc_krt_timer_start_us(struct neverc_krt_timer *t, unsigned int us)
{
	return neverc_krt_timer_start_ns(
		t, (s64)us * NEVERC_KRT_NSEC_PER_USEC);
}

int neverc_krt_timer_cancel(struct neverc_krt_timer *t)
{
	if (!t || !_neverc_krt_hrtimer_cancel) return -1;
	t->armed = 0;
	return _neverc_krt_hrtimer_cancel((struct hrtimer *)t->storage);
}

u64 neverc_krt_ktime_get_ns(void)
{
	return _neverc_krt_ktime_get ? _neverc_krt_ktime_get() : 0;
}

u64 neverc_krt_ktime_get_boot_ns(void)
{
	if (_neverc_krt_ktime_get_boot)
		return _neverc_krt_ktime_get_boot();
	if (_neverc_krt_ktime_get_offset)
		return (u64)_neverc_krt_ktime_get_offset(1); /* TK_OFFS_BOOT */
	return 0;
}

u64 neverc_krt_arch_counter_to_ns(u64 ticks)
{
	u32 freq = neverc_krt_arch_counter_freq();
	if (!freq) return 0;
	return (ticks / freq) * NEVERC_KRT_NSEC_PER_SEC +
	       (ticks % freq) * NEVERC_KRT_NSEC_PER_SEC / freq;
}

void neverc_krt_udelay(unsigned int us)
{
	u64 start = neverc_krt_arch_counter();
	u32 freq = neverc_krt_arch_counter_freq();
	u64 target = (u64)us * freq / NEVERC_KRT_USEC_PER_SEC;
	while (neverc_krt_arch_counter() - start < target)
		__asm__ __volatile__("yield" ::: "memory");
}

#undef NEVERC_KRT_TIMER_FORCE_INLINE

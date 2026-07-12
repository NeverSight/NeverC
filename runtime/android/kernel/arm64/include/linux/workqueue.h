/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_WORKQUEUE_H
#define _NEVERC_KRT_LINUX_WORKQUEUE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/timer.h>
#include <nvkmod_version.h>

struct workqueue_struct; /* opaque */
struct work_struct;

typedef void (*work_func_t)(struct work_struct *work);

/*
 * Exact work_struct layout for the official arm64 GKI configurations.
 *
 *   GKI version    sizeof(struct work_struct)
 *   ──────────────────────────────────────────
 *   5.10–6.6       48 bytes (core 32 + ANDROID_KABI_RESERVE(1)+(2) = 16)
 *   6.12–6.18      32 bytes (KABI reserves removed from work_struct)
 */
struct work_struct {
	unsigned long data;
	struct list_head entry;
	work_func_t func;
#if NEVERC_KRT_KERNEL < 612
	u64 __kabi_reserved1;
	u64 __kabi_reserved2;
#endif
};

/*
 * timer follows work immediately, so delayed_work cannot be an opaque
 * maximum-sized blob: queue_delayed_work_on() uses the target profile's
 * compile-time timer offset.
 */
struct delayed_work {
	struct work_struct work;
	struct timer_list timer;
	struct workqueue_struct *wq;
	int cpu;
	u32 __pad;
	u64 __kabi_reserved1;
	u64 __kabi_reserved2;
};

#if NEVERC_KRT_KERNEL < 612
_Static_assert(sizeof(struct work_struct) == 48,
	       "unexpected GKI 5.10-6.6 work_struct layout");
_Static_assert(__builtin_offsetof(struct delayed_work, timer) == 48,
	       "unexpected GKI 5.10-6.6 delayed_work timer offset");
_Static_assert(sizeof(struct delayed_work) == 136,
	       "unexpected GKI 5.10-6.6 delayed_work layout");
#else
_Static_assert(sizeof(struct work_struct) == 32,
	       "unexpected GKI 6.12+ work_struct layout");
_Static_assert(__builtin_offsetof(struct delayed_work, timer) == 32,
	       "unexpected GKI 6.12+ delayed_work timer offset");
_Static_assert(sizeof(struct delayed_work) == 104,
	       "unexpected GKI 6.12+ delayed_work layout");
#endif

/*
 * Upstream initializes an unqueued work item with WORK_STRUCT_NO_POOL, not
 * zero.  `data` is represented as unsigned long here because atomic_long_t has
 * the same arm64 layout; queueing helpers still perform atomic accesses.
 */
#define WORK_STRUCT_FLAG_BITS   4
#define WORK_STRUCT_COLOR_SHIFT WORK_STRUCT_FLAG_BITS
#define WORK_STRUCT_COLOR_BITS  4
#define WORK_STRUCT_PWQ_SHIFT \
	(WORK_STRUCT_COLOR_SHIFT + WORK_STRUCT_COLOR_BITS)
#define WORK_OFFQ_FLAG_BITS 1
#if NEVERC_KRT_KERNEL < 612
#define WORK_OFFQ_POOL_SHIFT \
	(WORK_STRUCT_COLOR_SHIFT + WORK_OFFQ_FLAG_BITS)
#else
#define WORK_OFFQ_DISABLE_BITS 16
#define WORK_OFFQ_POOL_SHIFT \
	(WORK_STRUCT_COLOR_SHIFT + WORK_OFFQ_FLAG_BITS + \
	 WORK_OFFQ_DISABLE_BITS)
#endif
#define WORK_OFFQ_POOL_BITS 31
#define WORK_OFFQ_POOL_NONE ((1UL << WORK_OFFQ_POOL_BITS) - 1)
#define WORK_STRUCT_NO_POOL (WORK_OFFQ_POOL_NONE << WORK_OFFQ_POOL_SHIFT)
#define WORK_DATA_INIT() WORK_STRUCT_NO_POOL

_Static_assert(WORK_DATA_INIT() != 0,
	       "work_struct must start in the no-pool state");
#if NEVERC_KRT_KERNEL < 612
_Static_assert(WORK_OFFQ_POOL_SHIFT == 5,
	       "unexpected GKI 5.10-6.6 off-queue work encoding");
#else
_Static_assert(WORK_OFFQ_POOL_SHIFT == 21,
	       "unexpected GKI 6.12+ off-queue work encoding");
#endif

#define __INIT_WORK(_work, _func)                                             \
	do {                                                                  \
		__builtin_memset((_work), 0, sizeof(*(_work)));               \
		(_work)->data = WORK_DATA_INIT();                             \
		INIT_LIST_HEAD(&(_work)->entry);                              \
		(_work)->func = (_func);                                      \
	} while (0)

#define INIT_WORK(_work, _func) __INIT_WORK((_work), (_func))

#define DECLARE_WORK(n, f)                                                    \
	struct work_struct n = {                                              \
		.data = WORK_DATA_INIT(),                                     \
		.entry = LIST_HEAD_INIT(n.entry),                             \
		.func = (f),                                                  \
	}

void delayed_work_timer_fn(struct timer_list *timer);

#define __INIT_DELAYED_WORK(_work, _func, _timer_flags)                       \
	do {                                                                  \
		__builtin_memset((_work), 0, sizeof(*(_work)));               \
		INIT_WORK(&(_work)->work, (_func));                            \
		timer_setup(&(_work)->timer, delayed_work_timer_fn,            \
			    (_timer_flags) | TIMER_IRQSAFE);                   \
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)                                       \
	__INIT_DELAYED_WORK((_work), (_func), 0)

#define INIT_DEFERRABLE_WORK(_work, _func)                                    \
	__INIT_DELAYED_WORK((_work), (_func), TIMER_DEFERRABLE)

#define DECLARE_DELAYED_WORK(name, _func)                                     \
	struct delayed_work name = {                                         \
		.work = {                                                     \
			.data = WORK_DATA_INIT(),                             \
			.entry = LIST_HEAD_INIT((name).work.entry),           \
			.func = (_func),                                      \
		},                                                             \
		.timer = __TIMER_INITIALIZER(delayed_work_timer_fn,            \
					     TIMER_IRQSAFE),                    \
	}

/* WQ flags for alloc_workqueue. */
#define WQ_UNBOUND       (1 << 1)
#define WQ_FREEZABLE     (1 << 2)
#define WQ_MEM_RECLAIM   (1 << 3)
#define WQ_HIGHPRI       (1 << 4)
#define WQ_CPU_INTENSIVE (1 << 5)

/*
 * 6.18+ renamed alloc_workqueue to alloc_workqueue_noprof.
 * The macro below provides source-level compat for both.
 */
#if NEVERC_KRT_KERNEL >= 618
struct workqueue_struct *alloc_workqueue_noprof(const char *fmt,
					       unsigned int flags,
					       int max_active, ...);
#define alloc_workqueue(...) alloc_workqueue_noprof(__VA_ARGS__)
#else
struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags,
					int max_active, ...);
#endif

void destroy_workqueue(struct workqueue_struct *wq);

#define create_singlethread_workqueue(name)                                   \
	alloc_workqueue("%s", WQ_UNBOUND | WQ_MEM_RECLAIM, 1, (name))

#define create_workqueue(name)                                                \
	alloc_workqueue("%s", WQ_MEM_RECLAIM, 1, (name))

/*
 * queue_work, schedule_work, queue_delayed_work, schedule_delayed_work,
 * mod_delayed_work are inline wrappers in ALL GKI kernels (5.10–6.18).
 * Only the *_on variants are real exports.
 *
 * WORK_CPU_UNBOUND is compared for exact equality inside __queue_work().
 * All supported official arm64 GKI profiles use CONFIG_NR_CPUS=32.  A vendor
 * kernel built with another value can override NEVERC_KRT_GKI_NR_CPUS.
 */
#ifndef NEVERC_KRT_GKI_NR_CPUS
#define NEVERC_KRT_GKI_NR_CPUS 32
#endif
#define WORK_CPU_UNBOUND NEVERC_KRT_GKI_NR_CPUS

#if NEVERC_KRT_KERNEL >= 618
extern struct workqueue_struct *system_percpu_wq;
#define NEVERC_KRT_SYSTEM_WORKQUEUE system_percpu_wq
#else
extern struct workqueue_struct *system_wq;
#define NEVERC_KRT_SYSTEM_WORKQUEUE system_wq
#endif

bool queue_work_on(int cpu, struct workqueue_struct *wq,
		   struct work_struct *work);
bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			   struct delayed_work *dwork, unsigned long delay);
bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			 struct delayed_work *dwork, unsigned long delay);

static __always_inline bool
queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	return queue_work_on(WORK_CPU_UNBOUND, wq, work);
}

static __always_inline bool schedule_work(struct work_struct *work)
{
	return queue_work_on(WORK_CPU_UNBOUND, NEVERC_KRT_SYSTEM_WORKQUEUE, work);
}

static __always_inline bool
queue_delayed_work(struct workqueue_struct *wq,
		   struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work_on(WORK_CPU_UNBOUND,
				     wq, dwork, delay);
}

static __always_inline bool
schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work_on(WORK_CPU_UNBOUND,
				     NEVERC_KRT_SYSTEM_WORKQUEUE, dwork, delay);
}

static __always_inline bool
mod_delayed_work(struct workqueue_struct *wq,
		 struct delayed_work *dwork, unsigned long delay)
{
	return mod_delayed_work_on(WORK_CPU_UNBOUND,
				   wq, dwork, delay);
}

bool cancel_work_sync(struct work_struct *work);
bool cancel_delayed_work(struct delayed_work *dwork);
bool cancel_delayed_work_sync(struct delayed_work *dwork);
bool flush_work(struct work_struct *work);

/*
 * 5.10–5.15: flush_workqueue exported directly.
 * 6.1+:      renamed to __flush_workqueue.
 */
#if NEVERC_KRT_KERNEL >= 601
void __flush_workqueue(struct workqueue_struct *wq);
#define flush_workqueue(wq) __flush_workqueue(wq)
#else
void flush_workqueue(struct workqueue_struct *wq);
#endif

#endif /* _NEVERC_KRT_LINUX_WORKQUEUE_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_WORKQUEUE_H
#define _NEVERC_KRT_LINUX_WORKQUEUE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/compiler.h>

struct workqueue_struct; /* opaque */

typedef void (*work_func_t)(struct work_struct *work);

/*
 * Minimal work_struct layout — stable core fields across GKI 5.10–6.12.
 *
 *   GKI version    sizeof(struct work_struct)
 *   ──────────────────────────────────────────
 *   5.10–6.6       48 bytes (core 32 + ANDROID_KABI_RESERVE(1)+(2) = 16)
 *   6.12           32 bytes (KABI reserves removed)
 *
 * CONFIG_LOCKDEP is disabled in GKI production builds, so lockdep_map
 * is never present.  The kernel only accesses data/entry/func through
 * pointer — never copies work_struct by value — so 32 bytes is safe
 * for queue_work / cancel_work_sync on all GKI versions.
 */
struct work_struct {
	unsigned long data;
	struct list_head entry;
	work_func_t func;
};

/*
 * delayed_work layout varies across GKI versions because both
 * work_struct KABI padding and timer_list size differ.
 * Use opaque storage sized for the largest variant (5.10 with KABI).
 *
 * Prefer neverc_krt_timer / neverc_krt_delayed_work from nvk_timer.h
 * for cross-version portable timer functionality.
 */
struct delayed_work {
	unsigned char __opaque[192];
};

/*
 * WORK_DATA_INIT: clear data field.  The kernel encodes flags and pool info
 * in the data field; zero is safe for static/stack-allocated work items
 * that have never been queued.
 */
#define WORK_DATA_INIT() 0

#define __INIT_WORK(_work, _func)                                             \
	do {                                                                  \
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

/* WQ flags for alloc_workqueue. */
#define WQ_UNBOUND       (1 << 1)
#define WQ_FREEZABLE     (1 << 2)
#define WQ_MEM_RECLAIM   (1 << 3)
#define WQ_HIGHPRI       (1 << 4)
#define WQ_CPU_INTENSIVE (1 << 5)

struct workqueue_struct *alloc_workqueue(const char *fmt, unsigned int flags,
					int max_active, ...);
void destroy_workqueue(struct workqueue_struct *wq);

#define create_singlethread_workqueue(name)                                   \
	alloc_workqueue("%s", WQ_UNBOUND | WQ_MEM_RECLAIM, 1, (name))

#define create_workqueue(name)                                                \
	alloc_workqueue("%s", WQ_MEM_RECLAIM, 1, (name))

bool queue_work(struct workqueue_struct *wq, struct work_struct *work);
bool queue_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay);

bool schedule_work(struct work_struct *work);
bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay);

bool cancel_work_sync(struct work_struct *work);
bool cancel_delayed_work(struct delayed_work *dwork);
bool cancel_delayed_work_sync(struct delayed_work *dwork);
void flush_work(struct work_struct *work);
void flush_workqueue(struct workqueue_struct *wq);

bool mod_delayed_work(struct workqueue_struct *wq,
		      struct delayed_work *dwork, unsigned long delay);

#endif /* _NEVERC_KRT_LINUX_WORKQUEUE_H */

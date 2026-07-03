/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_TIMER_H
#define _NEVERC_KRT_LINUX_TIMER_H

#include <linux/types.h>
#include <linux/jiffies.h>
#include <nvkmod_version.h>

/*
 * Minimal timer_list — core fields are stable across GKI 5.10–6.18.
 * 5.10–6.6 append ANDROID_KABI_RESERVE(1)+(2) (16 bytes) but the kernel
 * never accesses those reserves through external APIs, so the 40-byte
 * minimal layout is safe on all versions.
 */
struct timer_list {
	struct hlist_node entry;
	unsigned long expires;
	void (*function)(struct timer_list *);
	u32 flags;
	u32 _pad;
};

#define TIMER_IRQSAFE       0x00200000
#define TIMER_DEFERRABLE     0x00080000
#define TIMER_PINNED         0x00040000

#define __TIMER_INITIALIZER(_function, _flags) {                              \
		.entry = { .next = (void *)0 },                               \
		.function = (_function),                                      \
		.flags = (_flags),                                            \
	}

#define DEFINE_TIMER(_name, _function)                                        \
	struct timer_list _name = __TIMER_INITIALIZER(_function, 0)

__always_inline void
timer_setup(struct timer_list *timer,
	    void (*callback)(struct timer_list *), unsigned int flags)
{
	timer->entry.next = (void *)0;
	timer->entry.pprev = (void *)0;
	timer->function = callback;
	timer->flags = flags;
	timer->expires = 0;
}

int mod_timer(struct timer_list *timer, unsigned long expires);
void add_timer(struct timer_list *timer);

/*
 * del_timer / del_timer_sync export evolution:
 *   5.10–6.1:  del_timer ✓   del_timer_sync ✓
 *   6.6–6.18:  timer_delete ✓  timer_delete_sync ✓
 *   (verified from GKI android17-6.18 System.map __ksymtab)
 */
#if NEVERC_KRT_KERNEL >= 606
int timer_delete(struct timer_list *timer);
int timer_delete_sync(struct timer_list *timer);
#define del_timer(t) timer_delete(t)
#define del_timer_sync(t) timer_delete_sync(t)
#else
int del_timer(struct timer_list *timer);
int del_timer_sync(struct timer_list *timer);
#endif

__always_inline int timer_pending(const struct timer_list *timer)
{
	return timer->entry.pprev != (void *)0;
}

#endif /* _NEVERC_KRT_LINUX_TIMER_H */

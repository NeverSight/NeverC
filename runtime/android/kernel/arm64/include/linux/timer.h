/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_TIMER_H
#define _NEVERC_KRT_LINUX_TIMER_H

#include <linux/types.h>
#include <linux/jiffies.h>

struct timer_list {
	struct hlist_node entry;
	unsigned long expires;
	void (*function)(struct timer_list *);
	u32 flags;
	u32 _pad;
	/*
	 * GKI 5.10-6.6 add ANDROID_KABI_RESERVE(1) + (2) = 16 bytes
	 * after flags.  6.12 drops them.  Reserve the space so user
	 * code that allocates timer_list on the stack is safe on all
	 * GKI versions (sizeof must be >= 56).
	 */
	u64 _kabi_reserved[2];
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

static __always_inline void
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
int del_timer(struct timer_list *timer);
int del_timer_sync(struct timer_list *timer);
void add_timer(struct timer_list *timer);
int timer_pending(const struct timer_list *timer);

#endif /* _NEVERC_KRT_LINUX_TIMER_H */

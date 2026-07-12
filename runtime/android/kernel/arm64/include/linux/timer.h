/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_TIMER_H
#define _NEVERC_KRT_LINUX_TIMER_H

#include <linux/types.h>
#include <linux/jiffies.h>
#include <nvkmod_version.h>

/* Exact timer_list layout for the official arm64 GKI configurations. */
struct timer_list {
	struct hlist_node entry;
	unsigned long expires;
	void (*function)(struct timer_list *);
	u32 flags;
	u32 _pad;
#if NEVERC_KRT_KERNEL < 612
	u64 __kabi_reserved1;
	u64 __kabi_reserved2;
#endif
};

#if NEVERC_KRT_KERNEL < 612
_Static_assert(sizeof(struct timer_list) == 56,
	       "unexpected GKI 5.10-6.6 timer_list layout");
#else
_Static_assert(sizeof(struct timer_list) == 40,
	       "unexpected GKI 6.12+ timer_list layout");
#endif

#define TIMER_IRQSAFE       0x00200000
#define TIMER_PINNED        0x00100000
#define TIMER_DEFERRABLE    0x00080000

#define __TIMER_INITIALIZER(_function, _flags) {                              \
		.entry = { .next = (void *)0 },                               \
		.function = (_function),                                      \
		.flags = (_flags),                                            \
	}

#define DEFINE_TIMER(_name, _function)                                        \
	struct timer_list _name = __TIMER_INITIALIZER(_function, 0)

/*
 * timer_setup is a macro around the exported init helper.  Calling the helper
 * is required: it records the current CPU in timer->flags and initializes
 * debug-object state when enabled.
 */
struct lock_class_key;
#if NEVERC_KRT_KERNEL >= 618
void timer_init_key(struct timer_list *timer,
		    void (*callback)(struct timer_list *), unsigned int flags,
		    const char *name, struct lock_class_key *key);
#define timer_setup(timer, callback, flags)                                   \
	timer_init_key((timer), (callback), (flags), #timer,                  \
		       (struct lock_class_key *)0)
#else
void init_timer_key(struct timer_list *timer,
		    void (*callback)(struct timer_list *), unsigned int flags,
		    const char *name, struct lock_class_key *key);
#define timer_setup(timer, callback, flags)                                   \
	init_timer_key((timer), (callback), (flags), #timer,                   \
		       (struct lock_class_key *)0)
#endif

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

static __always_inline int timer_pending(const struct timer_list *timer)
{
	return timer->entry.pprev != (void *)0;
}

#endif /* _NEVERC_KRT_LINUX_TIMER_H */

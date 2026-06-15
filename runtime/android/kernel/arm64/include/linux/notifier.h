/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_NOTIFIER_H
#define _NVK_LINUX_NOTIFIER_H

#include <linux/types.h>

struct notifier_block;

typedef int (*notifier_fn_t)(struct notifier_block *nb, unsigned long action,
			     void *data);

struct notifier_block {
	notifier_fn_t notifier_call;
	struct notifier_block *next;
	int priority;
};

#define NOTIFY_DONE    0x0000
#define NOTIFY_OK      0x0001
#define NOTIFY_STOP_MASK 0x8000
#define NOTIFY_BAD     (NOTIFY_STOP_MASK | 0x0002)
#define NOTIFY_STOP    (NOTIFY_STOP_MASK | NOTIFY_OK)

int blocking_notifier_chain_register(void *nh, struct notifier_block *nb);
int blocking_notifier_chain_unregister(void *nh, struct notifier_block *nb);
int blocking_notifier_call_chain(void *nh, unsigned long val, void *v);

int atomic_notifier_chain_register(void *nh, struct notifier_block *nb);
int atomic_notifier_chain_unregister(void *nh, struct notifier_block *nb);
int atomic_notifier_call_chain(void *nh, unsigned long val, void *v);

int register_reboot_notifier(struct notifier_block *nb);
int unregister_reboot_notifier(struct notifier_block *nb);

#endif /* _NVK_LINUX_NOTIFIER_H */

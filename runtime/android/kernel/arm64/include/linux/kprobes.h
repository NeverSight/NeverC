/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_KPROBES_H
#define _NEVERC_KRT_LINUX_KPROBES_H

#include <linux/types.h>

#ifndef NEVERC_KRT_KP_SIZE
#define NEVERC_KRT_KP_SIZE 0x90
#endif

struct kprobe {
	unsigned char _head[40];   /* hlist(16) + list(16) + nmissed(8) */
	void *addr;                /* +40, filled by the kprobe framework */
	const char *symbol_name;   /* +48 */
	unsigned int offset;       /* +56 */
	unsigned int _pad;         /* +60 */
	int (*pre_handler)(struct kprobe *, void *); /* +64 */
	unsigned char _tail[NEVERC_KRT_KP_SIZE - 72];
};

extern int register_kprobe(struct kprobe *kp);
extern void unregister_kprobe(struct kprobe *kp);

#endif /* _NEVERC_KRT_LINUX_KPROBES_H */

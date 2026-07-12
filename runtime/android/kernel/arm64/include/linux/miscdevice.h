/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_MISCDEVICE_H
#define _NEVERC_KRT_LINUX_MISCDEVICE_H

#include <linux/types.h>

struct file_operations;
struct device;
struct attribute_group;

#define MISC_DYNAMIC_MINOR 255

struct miscdevice {
	int minor;
	const char *name;
	const struct file_operations *fops;
	struct list_head list;
	struct device *parent;
	struct device *this_device;
	const struct attribute_group **groups;
	const char *nodename;
	umode_t mode;
};

_Static_assert(sizeof(struct miscdevice) == 80,
	       "unexpected arm64 GKI miscdevice layout");
_Static_assert(__builtin_offsetof(struct miscdevice, fops) == 16,
	       "unexpected arm64 GKI miscdevice.fops offset");
_Static_assert(__builtin_offsetof(struct miscdevice, groups) == 56,
	       "unexpected arm64 GKI miscdevice.groups offset");

int misc_register(struct miscdevice *misc);
void misc_deregister(struct miscdevice *misc);

#endif /* _NEVERC_KRT_LINUX_MISCDEVICE_H */

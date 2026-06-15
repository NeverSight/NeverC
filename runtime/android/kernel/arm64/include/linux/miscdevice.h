/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_MISCDEVICE_H
#define _NVK_LINUX_MISCDEVICE_H

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

int misc_register(struct miscdevice *misc);
void misc_deregister(struct miscdevice *misc);

#endif /* _NVK_LINUX_MISCDEVICE_H */

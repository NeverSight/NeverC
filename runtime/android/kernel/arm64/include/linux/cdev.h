/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_CDEV_H
#define _NVK_LINUX_CDEV_H

#include <linux/types.h>

struct cdev; /* opaque */
struct file_operations;

int register_chrdev_region(dev_t from, unsigned count, const char *name);
int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count,
			const char *name);
void unregister_chrdev_region(dev_t from, unsigned count);

#endif /* _NVK_LINUX_CDEV_H */

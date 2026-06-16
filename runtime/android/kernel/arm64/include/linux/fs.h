/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_FS_H
#define _NEVERC_KRT_LINUX_FS_H

#include <linux/types.h>

struct file;        /* opaque */
struct inode;       /* opaque */
struct module;      /* opaque */
struct poll_table_struct;

struct file_operations {
	struct module *owner;
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
	void *_iter_pad[2]; /* read_iter / write_iter */
	void *_iopoll_pad;
	int (*iterate)(struct file *, void *);
	int (*iterate_shared)(struct file *, void *);
	unsigned int (*poll)(struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*mmap)(struct file *, struct vm_area_struct *);
	unsigned long mmap_supported_flags;
	int (*open)(struct inode *, struct file *);
	int (*flush)(struct file *, void *);
	int (*release)(struct inode *, struct file *);
};

#endif /* _NEVERC_KRT_LINUX_FS_H */

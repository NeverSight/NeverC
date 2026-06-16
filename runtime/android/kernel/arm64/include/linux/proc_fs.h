/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PROC_FS_H
#define _NEVERC_KRT_LINUX_PROC_FS_H

#include <linux/types.h>

struct proc_dir_entry; /* opaque */
struct file_operations;
struct seq_operations;
struct proc_ops;

/*
 * proc_create() is available on 5.10+.  On 5.6+ kernels the preferred
 * interface is proc_create() with struct proc_ops (not file_operations).
 * We declare both; use whichever matches your target.
 */
struct proc_ops {
	unsigned int proc_flags;
	int (*proc_open)(struct inode *, struct file *);
	ssize_t (*proc_read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*proc_write)(struct file *, const char __user *, size_t, loff_t *);
	loff_t (*proc_lseek)(struct file *, loff_t, int);
	int (*proc_release)(struct inode *, struct file *);
	long (*proc_ioctl)(struct file *, unsigned int, unsigned long);
	long (*proc_compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*proc_mmap)(struct file *, struct vm_area_struct *);
	unsigned long (*proc_get_unmapped_area)(struct file *, unsigned long,
					       unsigned long, unsigned long,
					       unsigned long);
};

struct proc_dir_entry *proc_create(const char *name, umode_t mode,
				   struct proc_dir_entry *parent,
				   const struct proc_ops *proc_ops);

struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
					struct proc_dir_entry *parent,
					const struct proc_ops *proc_ops,
					void *data);

struct proc_dir_entry *proc_mkdir(const char *name,
				  struct proc_dir_entry *parent);

void remove_proc_entry(const char *name, struct proc_dir_entry *parent);
void proc_remove(struct proc_dir_entry *de);

void *PDE_DATA(const struct inode *inode);

struct proc_dir_entry *proc_create_seq(const char *name, umode_t mode,
				       struct proc_dir_entry *parent,
				       const struct seq_operations *ops);

#endif /* _NEVERC_KRT_LINUX_PROC_FS_H */

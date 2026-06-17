/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PROC_FS_H
#define _NEVERC_KRT_LINUX_PROC_FS_H

#include <linux/types.h>
#include <stddef.h>

struct proc_dir_entry; /* opaque */
struct file_operations;
struct seq_operations;
struct proc_ops;
struct kiocb;              /* opaque */
struct iov_iter;           /* opaque */
struct poll_table_struct;  /* opaque */

typedef unsigned int __poll_t;

/*
 * proc_create() is available on 5.10+.  On 5.6+ kernels the preferred
 * interface is proc_create() with struct proc_ops (not file_operations).
 *
 * Field order verified against GKI 5.10 / 5.15 / 6.1 / 6.6 / 6.12
 * (gki_defconfig, CONFIG_COMPAT=y on arm64).
 */
struct proc_ops {
	unsigned int proc_flags;
	int (*proc_open)(struct inode *, struct file *);
	ssize_t (*proc_read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*proc_read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t (*proc_write)(struct file *, const char __user *, size_t, loff_t *);
	loff_t (*proc_lseek)(struct file *, loff_t, int);
	int (*proc_release)(struct inode *, struct file *);
	__poll_t (*proc_poll)(struct file *, struct poll_table_struct *);
	long (*proc_ioctl)(struct file *, unsigned int, unsigned long);
	long (*proc_compat_ioctl)(struct file *, unsigned int, unsigned long);
	int (*proc_mmap)(struct file *, struct vm_area_struct *);
	unsigned long (*proc_get_unmapped_area)(struct file *, unsigned long,
					       unsigned long, unsigned long,
					       unsigned long);
};

/* Verified against GKI 5.10–6.12 (gki_defconfig, CONFIG_COMPAT=y). */
_Static_assert(__builtin_offsetof(struct proc_ops, proc_lseek) == 40,
	       "proc_ops.proc_lseek offset mismatch");
_Static_assert(sizeof(struct proc_ops) == 96,
	       "proc_ops size mismatch for GKI arm64");

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

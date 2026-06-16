/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SEQ_FILE_H
#define _NEVERC_KRT_LINUX_SEQ_FILE_H

#include <linux/types.h>

struct seq_file;  /* opaque -- passed by pointer only */
struct file;
struct inode;

struct seq_operations {
	void *(*start)(struct seq_file *m, loff_t *pos);
	void  (*stop)(struct seq_file *m, void *v);
	void *(*next)(struct seq_file *m, void *v, loff_t *pos);
	int   (*show)(struct seq_file *m, void *v);
};

void seq_printf(struct seq_file *m, const char *fmt, ...);
void seq_puts(struct seq_file *m, const char *s);
void seq_putc(struct seq_file *m, char c);
int seq_write(struct seq_file *m, const void *data, size_t len);

int seq_open(struct file *file, const struct seq_operations *op);
ssize_t seq_read(struct file *file, char __user *buf, size_t size,
		 loff_t *ppos);
loff_t seq_lseek(struct file *file, loff_t offset, int whence);
int seq_release(struct inode *inode, struct file *file);

int single_open(struct file *file,
		int (*show)(struct seq_file *, void *), void *data);
int single_release(struct inode *inode, struct file *file);

#endif /* _NEVERC_KRT_LINUX_SEQ_FILE_H */

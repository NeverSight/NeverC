/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_DEBUGFS_H
#define _NEVERC_KRT_LINUX_DEBUGFS_H

#include <linux/types.h>

struct dentry; /* opaque */
struct file_operations;

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);
struct dentry *debugfs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops);
void debugfs_create_u32(const char *name, umode_t mode,
			struct dentry *parent, u32 *value);
void debugfs_create_u64(const char *name, umode_t mode,
			struct dentry *parent, u64 *value);
void debugfs_create_bool(const char *name, umode_t mode,
			 struct dentry *parent, bool *value);
void debugfs_remove(struct dentry *dentry);
void debugfs_remove_recursive(struct dentry *dentry);

#endif /* _NEVERC_KRT_LINUX_DEBUGFS_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_DEBUGFS_H
#define _NEVERC_KRT_LINUX_DEBUGFS_H

#include <linux/types.h>
#include <nvkmod_version.h>

struct dentry; /* opaque */
struct file_operations;

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);

/*
 * 6.18+: debugfs_create_file was split into debugfs_create_file_full
 * (with proxy fops) and debugfs_create_file_short.  The plain name is
 * no longer exported.
 */
#if NEVERC_KRT_KERNEL >= 618
struct dentry *debugfs_create_file_full(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops);
#define debugfs_create_file(name, mode, parent, data, fops) \
	debugfs_create_file_full(name, mode, parent, data, fops)
#else
struct dentry *debugfs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops);
#endif

void debugfs_create_u32(const char *name, umode_t mode,
			struct dentry *parent, u32 *value);
void debugfs_create_u64(const char *name, umode_t mode,
			struct dentry *parent, u64 *value);
void debugfs_create_bool(const char *name, umode_t mode,
			 struct dentry *parent, bool *value);

/*
 * debugfs_remove handles recursive removal in all GKI versions.
 * debugfs_remove_recursive was never exported — always an inline alias.
 */
void debugfs_remove(struct dentry *dentry);
#define debugfs_remove_recursive(dentry) debugfs_remove(dentry)

#endif /* _NEVERC_KRT_LINUX_DEBUGFS_H */

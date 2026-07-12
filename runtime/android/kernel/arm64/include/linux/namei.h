/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_NAMEI_H
#define _NEVERC_KRT_LINUX_NAMEI_H

#include <linux/types.h>

struct path;   /* opaque */
struct dentry; /* opaque */
struct inode;  /* opaque */
struct vfsmount; /* opaque */

/* Lookup flags. */
#define LOOKUP_FOLLOW     0x0001
#define LOOKUP_DIRECTORY  0x0002
#define LOOKUP_AUTOMOUNT  0x0004
#define LOOKUP_EMPTY      0x4000
#define LOOKUP_OPEN       0x0100
#define LOOKUP_CREATE     0x0200
#define LOOKUP_EXCL       0x0400

#ifdef NEVERC_KRT_NON_KMI_API
int kern_path(const char *name, unsigned int flags, struct path *path);
int user_path_at(int dfd, const char __user *name, unsigned flags,
		 struct path *path);
void path_put(struct path *path);

struct dentry *kern_path_create(int dfd, const char *pathname,
				struct path *path, unsigned int lookup_flags);
void done_path_create(struct path *path, struct dentry *dentry);
#endif

#endif /* _NEVERC_KRT_LINUX_NAMEI_H */

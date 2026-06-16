/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SECURITY_H
#define _NEVERC_KRT_LINUX_SECURITY_H

#include <linux/types.h>

struct cred;
struct file;
struct inode;
struct path;

int security_bprm_check(void *bprm);
int security_file_permission(struct file *file, int mask);
int security_inode_permission(struct inode *inode, int mask);
int security_task_fix_setuid(struct cred *new_cred,
			     const struct cred *old, int flags);

#endif /* _NEVERC_KRT_LINUX_SECURITY_H */

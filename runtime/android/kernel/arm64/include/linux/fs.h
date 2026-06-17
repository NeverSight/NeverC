/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_FS_H
#define _NEVERC_KRT_LINUX_FS_H

#include <linux/types.h>

struct file;        /* opaque */
struct inode;       /* opaque */
struct module;      /* opaque */
struct poll_table_struct;

/*
 * file_operations layout differs across GKI 5.10–6.12:
 *   - iterate() removed in 6.1 (replaced by iterate_shared)
 *   - mmap_supported_flags added in 5.18
 * Declared opaque; use function pointers via NEVERC_KRT_LOOKUP instead.
 */
struct file_operations;

#endif /* _NEVERC_KRT_LINUX_FS_H */

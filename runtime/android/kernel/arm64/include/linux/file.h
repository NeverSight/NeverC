/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_FILE_H
#define _NEVERC_KRT_LINUX_FILE_H

#include <linux/types.h>

struct file;  /* opaque */
struct path;  /* opaque */

struct file *fget(unsigned int fd);
#ifdef NEVERC_KRT_NON_KMI_API
struct file *fget_raw(unsigned int fd);
#endif
void fput(struct file *file);
int get_unused_fd_flags(unsigned flags);
void put_unused_fd(unsigned int fd);
void fd_install(unsigned int fd, struct file *file);

#ifdef NEVERC_KRT_NON_KMI_API
struct file *filp_open(const char *filename, int flags, umode_t mode);
#endif
int filp_close(struct file *filp, void *id);

#ifdef NEVERC_KRT_NON_KMI_API
ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos);
ssize_t kernel_write(struct file *file, const void *buf, size_t count,
		     loff_t *pos);
#endif

#endif /* _NEVERC_KRT_LINUX_FILE_H */

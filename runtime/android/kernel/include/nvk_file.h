/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_FILE_H
#define NVK_FILE_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

typedef void *(*nvk_filp_open_fn)(const char *filename, int flags, u16 mode);
typedef int   (*nvk_filp_close_fn)(void *filp, void *id);
typedef long  (*nvk_kernel_read_fn)(void *filp, void *buf,
				    size_t count, long long *pos);
typedef long  (*nvk_kernel_write_fn)(void *filp, const void *buf,
				     size_t count, long long *pos);
typedef long  (*nvk_vfs_read_fn)(void *filp, char __user *buf,
				 size_t count, long long *pos);
typedef long  (*nvk_vfs_write_fn)(void *filp, const char __user *buf,
				  size_t count, long long *pos);
typedef long long (*nvk_vfs_llseek_fn)(void *filp, long long offset, int whence);
typedef void  (*nvk_set_fs_fn)(unsigned long);
typedef unsigned long (*nvk_get_fs_fn)(void);
typedef int   (*nvk_vfs_stat_fn)(const char *filename, void *stat);

NVK_RT_VAR nvk_filp_open_fn    _nvk_filp_open;
NVK_RT_VAR nvk_filp_close_fn   _nvk_filp_close;
NVK_RT_VAR nvk_kernel_read_fn  _nvk_kernel_read;
NVK_RT_VAR nvk_kernel_write_fn _nvk_kernel_write;
NVK_RT_VAR nvk_vfs_llseek_fn   _nvk_vfs_llseek;
NVK_RT_VAR nvk_vfs_stat_fn     _nvk_vfs_stat;
NVK_RT_VAR int                 _nvk_file_inited;

#define NVK_O_RDONLY   0x0000
#define NVK_O_WRONLY   0x0001
#define NVK_O_RDWR     0x0002
#define NVK_O_CREAT    0x0040
#define NVK_O_TRUNC    0x0200
#define NVK_O_APPEND   0x0400

#define NVK_SEEK_SET   0
#define NVK_SEEK_CUR   1
#define NVK_SEEK_END   2

int nvk_file_init(void);


struct nvk_file {
	void *filp;
};

int nvk_file_open(struct nvk_file *f, const char *path, int flags);


void nvk_file_close(struct nvk_file *f);


long nvk_file_read(struct nvk_file *f, void *buf,
			   size_t count, long long *pos);


long nvk_file_write(struct nvk_file *f, const void *buf,
			    size_t count, long long *pos);


long long nvk_file_seek(struct nvk_file *f, long long offset,
			       int whence);


long nvk_file_size(struct nvk_file *f);


long nvk_file_read_all(const char *path, void *buf, size_t max_len);


long nvk_file_write_all(const char *path, const void *buf, size_t len);


int nvk_file_exists(const char *path);


#endif /* NVK_FILE_H */

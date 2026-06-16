/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_FILE_H
#define NEVERC_KRT_FILE_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

typedef void *(*neverc_krt_filp_open_fn)(const char *filename, int flags, u16 mode);
typedef int   (*neverc_krt_filp_close_fn)(void *filp, void *id);
typedef long  (*neverc_krt_kernel_read_fn)(void *filp, void *buf,
				    size_t count, long long *pos);
typedef long  (*neverc_krt_kernel_write_fn)(void *filp, const void *buf,
				     size_t count, long long *pos);
typedef long  (*neverc_krt_vfs_read_fn)(void *filp, char __user *buf,
				 size_t count, long long *pos);
typedef long  (*neverc_krt_vfs_write_fn)(void *filp, const char __user *buf,
				  size_t count, long long *pos);
typedef long long (*neverc_krt_vfs_llseek_fn)(void *filp, long long offset, int whence);
typedef void  (*neverc_krt_set_fs_fn)(unsigned long);
typedef unsigned long (*neverc_krt_get_fs_fn)(void);
typedef int   (*neverc_krt_vfs_stat_fn)(const char *filename, void *stat);

NEVERC_KRT_RT_VAR neverc_krt_filp_open_fn    _neverc_krt_filp_open;
NEVERC_KRT_RT_VAR neverc_krt_filp_close_fn   _neverc_krt_filp_close;
NEVERC_KRT_RT_VAR neverc_krt_kernel_read_fn  _neverc_krt_kernel_read;
NEVERC_KRT_RT_VAR neverc_krt_kernel_write_fn _neverc_krt_kernel_write;
NEVERC_KRT_RT_VAR neverc_krt_vfs_llseek_fn   _neverc_krt_vfs_llseek;
NEVERC_KRT_RT_VAR neverc_krt_vfs_stat_fn     _neverc_krt_vfs_stat;
NEVERC_KRT_RT_VAR int                 _neverc_krt_file_inited;

#define NEVERC_KRT_O_RDONLY   0x0000
#define NEVERC_KRT_O_WRONLY   0x0001
#define NEVERC_KRT_O_RDWR     0x0002
#define NEVERC_KRT_O_CREAT    0x0040
#define NEVERC_KRT_O_TRUNC    0x0200
#define NEVERC_KRT_O_APPEND   0x0400

#define NEVERC_KRT_SEEK_SET   0
#define NEVERC_KRT_SEEK_CUR   1
#define NEVERC_KRT_SEEK_END   2

int neverc_krt_file_init(void);


struct neverc_krt_file {
	void *filp;
};

int neverc_krt_file_open(struct neverc_krt_file *f, const char *path, int flags);


void neverc_krt_file_close(struct neverc_krt_file *f);


long neverc_krt_file_read(struct neverc_krt_file *f, void *buf,
			   size_t count, long long *pos);


long neverc_krt_file_write(struct neverc_krt_file *f, const void *buf,
			    size_t count, long long *pos);


long long neverc_krt_file_seek(struct neverc_krt_file *f, long long offset,
			       int whence);


long neverc_krt_file_size(struct neverc_krt_file *f);


long neverc_krt_file_read_all(const char *path, void *buf, size_t max_len);


long neverc_krt_file_write_all(const char *path, const void *buf, size_t len);


int neverc_krt_file_exists(const char *path);


#endif /* NEVERC_KRT_FILE_H */

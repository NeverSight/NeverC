/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_FILE_H
#define NEVERC_KRT_FILE_H

#include <linux/types.h>

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

struct neverc_krt_file_stat {
	long long size;
	u32       mode;
	u32       uid;
	u32       gid;
};

int neverc_krt_file_stat(const char *path, struct neverc_krt_file_stat *out);

#endif /* NEVERC_KRT_FILE_H */

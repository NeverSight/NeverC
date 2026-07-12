/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_file.c — kernel file I/O operations. */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal types ---- */

typedef void *(*neverc_krt_filp_open_fn)(const char *filename, int flags, u16 mode);
typedef int   (*neverc_krt_filp_close_fn)(void *filp, void *id);
typedef long  (*neverc_krt_kernel_read_fn)(void *filp, void *buf,
					   size_t count, long long *pos);
typedef long  (*neverc_krt_kernel_write_fn)(void *filp, const void *buf,
					    size_t count, long long *pos);
typedef long long (*neverc_krt_vfs_llseek_fn)(void *filp, long long offset,
					      int whence);
/*
 * vfs_stat (5.10-6.12): int vfs_stat(const char *, struct kstat *)
 * vfs_fstatat (6.12+):  int vfs_fstatat(int dfd, const char *, struct kstat *, int)
 * vfs_stat was removed in 6.18.
 */
typedef int   (*neverc_krt_vfs_fstatat_fn)(int dfd, const char *filename,
					   void *stat, int flag);
typedef int   (*neverc_krt_vfs_stat_fn)(const char *filename, void *stat);

/* ---- internal variables (file-local) ---- */

static neverc_krt_filp_open_fn      _neverc_krt_filp_open;
static neverc_krt_filp_close_fn     _neverc_krt_filp_close;
static neverc_krt_kernel_read_fn    _neverc_krt_kernel_read;
static neverc_krt_kernel_write_fn   _neverc_krt_kernel_write;
static neverc_krt_vfs_llseek_fn     _neverc_krt_vfs_llseek;
static neverc_krt_vfs_fstatat_fn    _neverc_krt_vfs_fstatat;
static neverc_krt_vfs_stat_fn       _neverc_krt_vfs_stat;
static int                          _neverc_krt_file_inited;

int neverc_krt_file_init(void)
{
	if (_neverc_krt_file_inited) return 0;

	_neverc_krt_filp_open = (neverc_krt_filp_open_fn)NEVERC_KRT_LOOKUP("filp_open");
	_neverc_krt_filp_close = (neverc_krt_filp_close_fn)NEVERC_KRT_LOOKUP("filp_close");

	_neverc_krt_kernel_read = (neverc_krt_kernel_read_fn)NEVERC_KRT_LOOKUP("kernel_read");
	_neverc_krt_kernel_write = (neverc_krt_kernel_write_fn)NEVERC_KRT_LOOKUP("kernel_write");
	_neverc_krt_vfs_llseek = (neverc_krt_vfs_llseek_fn)NEVERC_KRT_LOOKUP("vfs_llseek");
	_neverc_krt_vfs_fstatat =
		(neverc_krt_vfs_fstatat_fn)NEVERC_KRT_LOOKUP("vfs_fstatat");
	if (!_neverc_krt_vfs_fstatat)
		_neverc_krt_vfs_stat =
			(neverc_krt_vfs_stat_fn)NEVERC_KRT_LOOKUP("vfs_stat");

	if (!_neverc_krt_filp_open)
		return -1;

	_neverc_krt_file_inited = 1;
	return 0;
}

int neverc_krt_file_open(struct neverc_krt_file *f, const char *path, int flags)
{
	if (!_neverc_krt_filp_open) return -1;

	f->filp = _neverc_krt_filp_open(path, flags, 0644);
	if (!f->filp || (long)f->filp < 0) {
		int err = (int)(long)f->filp;
		f->filp = (void *)0;
		return err ? err : -1;
	}
	return 0;
}

void neverc_krt_file_close(struct neverc_krt_file *f)
{
	if (!f || !f->filp) return;

	if (_neverc_krt_filp_close)
		_neverc_krt_filp_close(f->filp, (void *)0);
	f->filp = (void *)0;
}

long neverc_krt_file_read(struct neverc_krt_file *f, void *buf,
			   size_t count, long long *pos)
{
	if (!f || !f->filp) return -1;

	if (_neverc_krt_kernel_read)
		return _neverc_krt_kernel_read(f->filp, buf, count, pos);

	return -2;
}

long neverc_krt_file_write(struct neverc_krt_file *f, const void *buf,
			    size_t count, long long *pos)
{
	if (!f || !f->filp) return -1;

	if (_neverc_krt_kernel_write)
		return _neverc_krt_kernel_write(f->filp, buf, count, pos);

	return -2;
}

long long neverc_krt_file_seek(struct neverc_krt_file *f, long long offset,
			       int whence)
{
	if (!f || !f->filp || !_neverc_krt_vfs_llseek) return -1;
	return _neverc_krt_vfs_llseek(f->filp, offset, whence);
}

long neverc_krt_file_size(struct neverc_krt_file *f)
{
	if (!f || !f->filp || !_neverc_krt_vfs_llseek) return -1;

	long long cur = _neverc_krt_vfs_llseek(f->filp, 0, NEVERC_KRT_SEEK_CUR);
	long long end = _neverc_krt_vfs_llseek(f->filp, 0, NEVERC_KRT_SEEK_END);
	_neverc_krt_vfs_llseek(f->filp, cur, NEVERC_KRT_SEEK_SET);
	return (long)end;
}

long neverc_krt_file_read_all(const char *path, void *buf, size_t max_len)
{
	struct neverc_krt_file f;
	long long pos = 0;
	long ret;

	ret = neverc_krt_file_open(&f, path, NEVERC_KRT_O_RDONLY);
	if (ret) return ret;

	ret = neverc_krt_file_read(&f, buf, max_len, &pos);
	neverc_krt_file_close(&f);
	return ret;
}

long neverc_krt_file_write_all(const char *path, const void *buf, size_t len)
{
	struct neverc_krt_file f;
	long long pos = 0;
	long ret;

	ret = neverc_krt_file_open(&f, path, NEVERC_KRT_O_WRONLY | NEVERC_KRT_O_CREAT | NEVERC_KRT_O_TRUNC);
	if (ret) return ret;

	ret = neverc_krt_file_write(&f, buf, len, &pos);
	neverc_krt_file_close(&f);
	return ret;
}

int neverc_krt_file_exists(const char *path)
{
	struct neverc_krt_file f;
	int ret = neverc_krt_file_open(&f, path, NEVERC_KRT_O_RDONLY);
	if (ret) return 0;
	neverc_krt_file_close(&f);
	return 1;
}

#define _NEVERC_KRT_AT_FDCWD (-100)

static int _neverc_krt_do_stat(const char *path, unsigned char *buf, int bufsz)
{
	__builtin_memset(buf, 0, bufsz);
	if (_neverc_krt_vfs_fstatat)
		return _neverc_krt_vfs_fstatat(_NEVERC_KRT_AT_FDCWD, path, buf, 0);
	if (_neverc_krt_vfs_stat)
		return _neverc_krt_vfs_stat(path, buf);
	return -1;
}

int neverc_krt_file_stat(const char *path, struct neverc_krt_file_stat *out)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned char kstat_buf[256];

	if (!out || !path) return -1;

	out->size = 0;
	out->mode = 0;
	out->uid = 0;
	out->gid = 0;

	if (_neverc_krt_vfs_fstatat || _neverc_krt_vfs_stat) {
		layout = _neverc_krt_get_gki_layout();
		if (!layout->kstat_size ||
		    layout->kstat_size > sizeof(kstat_buf))
			return -1;

		int ret = _neverc_krt_do_stat(path, kstat_buf, sizeof(kstat_buf));
		if (ret) return ret;

		__builtin_memcpy(&out->mode,
				 kstat_buf + layout->kstat_mode,
				 sizeof(out->mode));
		__builtin_memcpy(&out->uid,
				 kstat_buf + layout->kstat_uid,
				 sizeof(out->uid));
		__builtin_memcpy(&out->gid,
				 kstat_buf + layout->kstat_gid,
				 sizeof(out->gid));
		__builtin_memcpy(&out->size,
				 kstat_buf + layout->kstat_file_size,
				 sizeof(out->size));
		return 0;
	}

	struct neverc_krt_file f;
	int ret = neverc_krt_file_open(&f, path, NEVERC_KRT_O_RDONLY);
	if (ret) return ret;
	out->size = neverc_krt_file_size(&f);
	neverc_krt_file_close(&f);
	return 0;
}


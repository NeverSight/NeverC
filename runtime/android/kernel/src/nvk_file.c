/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_file.c — kernel file I/O operations. */
#include <nvk.h>

/* ---- internal types ---- */

typedef void *(*neverc_krt_filp_open_fn)(const char *filename, int flags, u16 mode);
typedef int   (*neverc_krt_filp_close_fn)(void *filp, void *id);
typedef long  (*neverc_krt_kernel_read_fn)(void *filp, void *buf,
					   size_t count, long long *pos);
typedef long  (*neverc_krt_kernel_write_fn)(void *filp, const void *buf,
					    size_t count, long long *pos);
typedef long long (*neverc_krt_vfs_llseek_fn)(void *filp, long long offset,
					      int whence);
typedef int   (*neverc_krt_vfs_stat_fn)(const char *filename, void *stat);

/* ---- internal variables (file-local) ---- */

static neverc_krt_filp_open_fn    _neverc_krt_filp_open;
static neverc_krt_filp_close_fn   _neverc_krt_filp_close;
static neverc_krt_kernel_read_fn  _neverc_krt_kernel_read;
static neverc_krt_kernel_write_fn _neverc_krt_kernel_write;
static neverc_krt_vfs_llseek_fn   _neverc_krt_vfs_llseek;
static neverc_krt_vfs_stat_fn     _neverc_krt_vfs_stat;
static int                        _neverc_krt_file_inited;

int neverc_krt_file_init(void)
{
	if (_neverc_krt_file_inited) return 0;

	_neverc_krt_filp_open = (neverc_krt_filp_open_fn)NEVERC_KRT_LOOKUP("filp_open");
	_neverc_krt_filp_close = (neverc_krt_filp_close_fn)NEVERC_KRT_LOOKUP("filp_close");

	_neverc_krt_kernel_read = (neverc_krt_kernel_read_fn)NEVERC_KRT_LOOKUP("kernel_read");
	_neverc_krt_kernel_write = (neverc_krt_kernel_write_fn)NEVERC_KRT_LOOKUP("kernel_write");
	_neverc_krt_vfs_llseek = (neverc_krt_vfs_llseek_fn)NEVERC_KRT_LOOKUP("vfs_llseek");
	_neverc_krt_vfs_stat = (neverc_krt_vfs_stat_fn)NEVERC_KRT_LOOKUP("vfs_stat");

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


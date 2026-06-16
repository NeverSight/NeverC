/* SPDX-License-Identifier: GPL-2.0 */
/* nvk_file.c — implementations extracted from nvk_file.h. */
#include <nvk.h>

int nvk_file_init(void)
{
	if (_nvk_file_inited) return 0;

	_nvk_filp_open = (nvk_filp_open_fn)NVK_LOOKUP("filp_open");
	_nvk_filp_close = (nvk_filp_close_fn)NVK_LOOKUP("filp_close");

	_nvk_kernel_read = (nvk_kernel_read_fn)NVK_LOOKUP("kernel_read");
	_nvk_kernel_write = (nvk_kernel_write_fn)NVK_LOOKUP("kernel_write");
	_nvk_vfs_llseek = (nvk_vfs_llseek_fn)NVK_LOOKUP("vfs_llseek");
	_nvk_vfs_stat = (nvk_vfs_stat_fn)NVK_LOOKUP("vfs_stat");

	if (!_nvk_filp_open)
		return -1;

	_nvk_file_inited = 1;
	return 0;
}

int nvk_file_open(struct nvk_file *f, const char *path, int flags)
{
	if (!_nvk_filp_open) return -1;

	f->filp = _nvk_filp_open(path, flags, 0644);
	if (!f->filp || (long)f->filp < 0) {
		int err = (int)(long)f->filp;
		f->filp = (void *)0;
		return err ? err : -1;
	}
	return 0;
}

void nvk_file_close(struct nvk_file *f)
{
	if (!f || !f->filp) return;

	if (_nvk_filp_close)
		_nvk_filp_close(f->filp, (void *)0);
	f->filp = (void *)0;
}

long nvk_file_read(struct nvk_file *f, void *buf,
			   size_t count, long long *pos)
{
	if (!f || !f->filp) return -1;

	if (_nvk_kernel_read)
		return _nvk_kernel_read(f->filp, buf, count, pos);

	return -2;
}

long nvk_file_write(struct nvk_file *f, const void *buf,
			    size_t count, long long *pos)
{
	if (!f || !f->filp) return -1;

	if (_nvk_kernel_write)
		return _nvk_kernel_write(f->filp, buf, count, pos);

	return -2;
}

long long nvk_file_seek(struct nvk_file *f, long long offset,
			       int whence)
{
	if (!f || !f->filp || !_nvk_vfs_llseek) return -1;
	return _nvk_vfs_llseek(f->filp, offset, whence);
}

long nvk_file_size(struct nvk_file *f)
{
	if (!f || !f->filp || !_nvk_vfs_llseek) return -1;

	long long cur = _nvk_vfs_llseek(f->filp, 0, NVK_SEEK_CUR);
	long long end = _nvk_vfs_llseek(f->filp, 0, NVK_SEEK_END);
	_nvk_vfs_llseek(f->filp, cur, NVK_SEEK_SET);
	return (long)end;
}

long nvk_file_read_all(const char *path, void *buf, size_t max_len)
{
	struct nvk_file f;
	long long pos = 0;
	long ret;

	ret = nvk_file_open(&f, path, NVK_O_RDONLY);
	if (ret) return ret;

	ret = nvk_file_read(&f, buf, max_len, &pos);
	nvk_file_close(&f);
	return ret;
}

long nvk_file_write_all(const char *path, const void *buf, size_t len)
{
	struct nvk_file f;
	long long pos = 0;
	long ret;

	ret = nvk_file_open(&f, path, NVK_O_WRONLY | NVK_O_CREAT | NVK_O_TRUNC);
	if (ret) return ret;

	ret = nvk_file_write(&f, buf, len, &pos);
	nvk_file_close(&f);
	return ret;
}

int nvk_file_exists(const char *path)
{
	struct nvk_file f;
	int ret = nvk_file_open(&f, path, NVK_O_RDONLY);
	if (ret) return 0;
	nvk_file_close(&f);
	return 1;
}


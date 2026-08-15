/* SPDX-License-Identifier: GPL-2.0 */
/* Profile-aware access to opaque VFS filename, path and inode metadata. */
#if defined(NEVERC_KRT_INODE_METADATA_HOST_TEST)
#include "../tools/test-inode-metadata-shim.h"
#else
#include <nvk.h>
#include "nvk_internal.h"
#endif

#define _NEVERC_KRT_NSEC_PER_SEC 1000000000U

struct _neverc_krt_inode_field {
	unsigned long offset;
	unsigned long width;
};

struct _neverc_krt_inode_times_raw {
	s64 atime_sec;
	u64 atime_nsec;
	s64 mtime_sec;
	u64 mtime_nsec;
};

struct inode;

typedef struct inode *(*_neverc_krt_igrab_fn)(struct inode *inode);
typedef void (*_neverc_krt_iput_fn)(struct inode *inode);

static _neverc_krt_igrab_fn _neverc_krt_igrab;
static _neverc_krt_iput_fn _neverc_krt_iput;

static int _neverc_krt_field_fits(unsigned long object_size,
				  unsigned long offset,
				  unsigned long width)
{
	return object_size != 0 && width != 0 && offset <= object_size &&
	       width <= object_size - offset;
}

static int _neverc_krt_fields_overlap(
	const struct _neverc_krt_inode_field *left,
	const struct _neverc_krt_inode_field *right)
{
	return !(left->offset + left->width <= right->offset ||
		 right->offset + right->width <= left->offset);
}

static int _neverc_krt_private_layout_allowed(void)
{
	int match = neverc_krt_check_kernel_match();

	return match == NEVERC_KRT_VER_EXACT ||
	       match == NEVERC_KRT_VER_COMPAT;
}

static int _neverc_krt_inode_times_contract(
	const struct neverc_krt_gki_layout **layout_out)
{
	const struct neverc_krt_gki_layout *layout;
	struct _neverc_krt_inode_field fields[4];
	unsigned int i;
	unsigned int j;

	if (!_neverc_krt_private_layout_allowed())
		return -1;
	layout = _neverc_krt_get_gki_layout();
	if (!layout)
		return -1;

	fields[0].offset = layout->inode_atime_sec;
	fields[0].width = layout->inode_atime_sec_size;
	fields[1].offset = layout->inode_mtime_sec;
	fields[1].width = layout->inode_mtime_sec_size;
	fields[2].offset = layout->inode_atime_nsec;
	fields[2].width = layout->inode_atime_nsec_size;
	fields[3].offset = layout->inode_mtime_nsec;
	fields[3].width = layout->inode_mtime_nsec_size;

	if (fields[0].width != sizeof(s64) ||
	    fields[1].width != sizeof(s64) ||
	    (fields[2].width != sizeof(u32) &&
	     fields[2].width != sizeof(u64)) ||
	    (fields[3].width != sizeof(u32) &&
	     fields[3].width != sizeof(u64)))
		return -1;
	for (i = 0; i < 4; i++) {
		if (!_neverc_krt_field_fits(layout->inode_size,
					   fields[i].offset, fields[i].width))
			return -1;
		for (j = i + 1; j < 4; j++)
			if (_neverc_krt_fields_overlap(&fields[i], &fields[j]))
				return -1;
	}

	*layout_out = layout;
	return 0;
}

static int _neverc_krt_inode_write_nsec(unsigned char *inode,
					unsigned long offset,
					unsigned long width, u64 value)
{
	u64 wide_value = value;

	if (width == sizeof(u32)) {
		u32 narrow_value;

		if (value > (u64)(u32)-1)
			return -1;
		narrow_value = (u32)value;
		return neverc_krt_mem_write(inode + offset, &narrow_value,
					    sizeof(narrow_value)) ? -1 : 0;
	}
	return neverc_krt_mem_write(inode + offset, &wide_value,
				    sizeof(wide_value)) ? -1 : 0;
}

static int _neverc_krt_inode_read_nsec_raw(const unsigned char *inode,
					   unsigned long offset,
					   unsigned long width, u64 *out)
{
	if (width == sizeof(u32)) {
		u32 narrow_value;

		if (neverc_krt_mem_read(&narrow_value, inode + offset,
					 sizeof(narrow_value)))
			return -1;
		*out = narrow_value;
		return 0;
	}
	return neverc_krt_mem_read(out, inode + offset, sizeof(*out)) ? -1 : 0;
}

static int _neverc_krt_inode_read_times_raw_layout(
	const struct neverc_krt_gki_layout *layout,
	const unsigned char *inode, struct _neverc_krt_inode_times_raw *out)
{
	struct _neverc_krt_inode_times_raw value = {0};

	if (neverc_krt_mem_read(&value.atime_sec,
				inode + layout->inode_atime_sec,
				sizeof(value.atime_sec)) ||
	    neverc_krt_mem_read(&value.mtime_sec,
				inode + layout->inode_mtime_sec,
				sizeof(value.mtime_sec)) ||
	    _neverc_krt_inode_read_nsec_raw(inode, layout->inode_atime_nsec,
					     layout->inode_atime_nsec_size,
					     &value.atime_nsec) ||
	    _neverc_krt_inode_read_nsec_raw(inode, layout->inode_mtime_nsec,
					     layout->inode_mtime_nsec_size,
					     &value.mtime_nsec))
		return -1;
	*out = value;
	return 0;
}

static int _neverc_krt_inode_read_times_layout(
	const struct neverc_krt_gki_layout *layout,
	const unsigned char *inode, struct neverc_krt_inode_times *out)
{
	struct _neverc_krt_inode_times_raw raw;
	struct neverc_krt_inode_times value;

	if (_neverc_krt_inode_read_times_raw_layout(layout, inode, &raw) ||
	    raw.atime_nsec >= _NEVERC_KRT_NSEC_PER_SEC ||
	    raw.mtime_nsec >= _NEVERC_KRT_NSEC_PER_SEC)
		return -1;
	value.atime_sec = raw.atime_sec;
	value.atime_nsec = (u32)raw.atime_nsec;
	value.mtime_sec = raw.mtime_sec;
	value.mtime_nsec = (u32)raw.mtime_nsec;
	*out = value;
	return 0;
}

/* Attempt every scalar so rollback does not stop after one nofault failure. */
static int _neverc_krt_inode_write_times_layout(
	const struct neverc_krt_gki_layout *layout, unsigned char *inode,
	const struct _neverc_krt_inode_times_raw *value)
{
	int failed = 0;

	if (neverc_krt_mem_write(inode + layout->inode_atime_sec,
				 &value->atime_sec, sizeof(value->atime_sec)))
		failed = 1;
	if (neverc_krt_mem_write(inode + layout->inode_mtime_sec,
				 &value->mtime_sec, sizeof(value->mtime_sec)))
		failed = 1;
	if (_neverc_krt_inode_write_nsec(inode, layout->inode_atime_nsec,
					 layout->inode_atime_nsec_size,
					 value->atime_nsec))
		failed = 1;
	if (_neverc_krt_inode_write_nsec(inode, layout->inode_mtime_nsec,
					 layout->inode_mtime_nsec_size,
					 value->mtime_nsec))
		failed = 1;
	return failed ? -1 : 0;
}

static int _neverc_krt_path_inode_contract(
	const struct neverc_krt_gki_layout **layout_out)
{
	const struct neverc_krt_gki_layout *layout;

	if (!_neverc_krt_private_layout_allowed())
		return -1;
	layout = _neverc_krt_get_gki_layout();
	if (!layout ||
	    layout->path_size != sizeof(struct neverc_krt_path_storage) ||
	    layout->path_dentry_size != sizeof(void *) ||
	    layout->dentry_inode_size != sizeof(void *) ||
	    !_neverc_krt_field_fits(layout->path_size,
				    layout->path_dentry,
				    layout->path_dentry_size) ||
	    !_neverc_krt_field_fits(layout->dentry_size,
				    layout->dentry_inode,
				    layout->dentry_inode_size))
		return -1;
	*layout_out = layout;
	return 0;
}

static int _neverc_krt_filename_name_contract(
	const struct neverc_krt_gki_layout **layout_out)
{
	const struct neverc_krt_gki_layout *layout;

	if (!_neverc_krt_private_layout_allowed())
		return -1;
	layout = _neverc_krt_get_gki_layout();
	if (!layout || layout->filename_name_size != sizeof(const char *) ||
	    !_neverc_krt_field_fits(layout->filename_size,
				    layout->filename_name,
				    layout->filename_name_size))
		return -1;
	*layout_out = layout;
	return 0;
}

static int _neverc_krt_inode_ref_functions(
	_neverc_krt_igrab_fn *igrab_out, _neverc_krt_iput_fn *iput_out)
{
	_neverc_krt_igrab_fn igrab =
		__atomic_load_n(&_neverc_krt_igrab, __ATOMIC_ACQUIRE);
	_neverc_krt_iput_fn iput =
		__atomic_load_n(&_neverc_krt_iput, __ATOMIC_ACQUIRE);

	if (!igrab || !iput) {
		igrab = (_neverc_krt_igrab_fn)NEVERC_KRT_LOOKUP("igrab");
		iput = (_neverc_krt_iput_fn)NEVERC_KRT_LOOKUP("iput");
		/* Do not publish a half-resolved pair. */
		if (!igrab || !iput)
			return -1;
		__atomic_store_n(&_neverc_krt_iput, iput, __ATOMIC_RELEASE);
		__atomic_store_n(&_neverc_krt_igrab, igrab, __ATOMIC_RELEASE);
	}
	*igrab_out = igrab;
	*iput_out = iput;
	return 0;
}

int neverc_krt_inode_get_times(const void *opaque_inode,
			       struct neverc_krt_inode_times *out)
{
	const struct neverc_krt_gki_layout *layout;
	const unsigned char *inode = (const unsigned char *)opaque_inode;

	if (!out)
		return -1;
	__builtin_memset(out, 0, sizeof(*out));
	if (!inode)
		return -1;
	if (_neverc_krt_inode_times_contract(&layout))
		return -2;
	if (_neverc_krt_inode_read_times_layout(layout, inode, out)) {
		__builtin_memset(out, 0, sizeof(*out));
		return -3;
	}
	return 0;
}

int neverc_krt_inode_set_times(void *opaque_inode,
			       s64 atime_sec, u32 atime_nsec,
			       s64 mtime_sec, u32 mtime_nsec)
{
	const struct neverc_krt_gki_layout *layout;
	struct _neverc_krt_inode_times_raw original;
	struct _neverc_krt_inode_times_raw replacement = {
		.atime_sec = atime_sec,
		.atime_nsec = atime_nsec,
		.mtime_sec = mtime_sec,
		.mtime_nsec = mtime_nsec,
	};
	unsigned char *inode = (unsigned char *)opaque_inode;

	if (!inode || atime_nsec >= _NEVERC_KRT_NSEC_PER_SEC ||
	    mtime_nsec >= _NEVERC_KRT_NSEC_PER_SEC)
		return -1;
	if (_neverc_krt_inode_times_contract(&layout))
		return -2;
	if (_neverc_krt_inode_read_times_raw_layout(layout, inode, &original))
		return -3;
	if (!_neverc_krt_inode_write_times_layout(layout, inode, &replacement))
		return 0;
	/* A failed nofault write must not silently publish a mixed timestamp
	 * tuple.  Best-effort restore every original scalar; -4 tells callers the
	 * rollback itself could not be proven. */
	if (_neverc_krt_inode_write_times_layout(layout, inode, &original))
		return -4;
	return -3;
}

const char *neverc_krt_filename_name(const void *opaque_filename)
{
	const struct neverc_krt_gki_layout *layout;
	const char *name = (const char *)0;

	if (!opaque_filename ||
	    _neverc_krt_filename_name_contract(&layout) ||
	    neverc_krt_mem_read(
		    &name,
		    (const unsigned char *)opaque_filename + layout->filename_name,
		    sizeof(name)))
		return (const char *)0;
	return name;
}

int neverc_krt_filename_name_available(void)
{
	const struct neverc_krt_gki_layout *layout;

	return _neverc_krt_filename_name_contract(&layout) == 0;
}

int neverc_krt_path_storage_available(void)
{
	const struct neverc_krt_gki_layout *layout;

	return _neverc_krt_path_inode_contract(&layout) == 0;
}

void *neverc_krt_path_inode_get(const void *opaque_path)
{
	const struct neverc_krt_gki_layout *layout;
	_neverc_krt_igrab_fn igrab;
	_neverc_krt_iput_fn iput;
	void *dentry = (void *)0;
	void *inode = (void *)0;

	if (!opaque_path || _neverc_krt_path_inode_contract(&layout) ||
	    _neverc_krt_inode_ref_functions(&igrab, &iput))
		return (void *)0;
	(void)iput;
	if (neverc_krt_mem_read(
			&dentry,
			(const unsigned char *)opaque_path + layout->path_dentry,
			sizeof(dentry)) ||
	    !dentry ||
	    neverc_krt_mem_read(
			&inode,
			(const unsigned char *)dentry + layout->dentry_inode,
			sizeof(inode)) ||
	    !inode)
		return (void *)0;
	return (void *)igrab((struct inode *)inode);
}

void neverc_krt_inode_put(void *opaque_inode)
{
	_neverc_krt_igrab_fn igrab;
	_neverc_krt_iput_fn iput;

	if (!opaque_inode || _neverc_krt_inode_ref_functions(&igrab, &iput))
		return;
	(void)igrab;
	iput((struct inode *)opaque_inode);
}

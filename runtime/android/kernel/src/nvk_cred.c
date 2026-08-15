/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal typedefs ---- */

typedef void *(*neverc_krt_prepare_creds_fn)(void);
typedef int   (*neverc_krt_commit_creds_fn)(void *);
typedef void  (*neverc_krt_abort_creds_fn)(void *);
typedef void  (*neverc_krt_put_cred_fn)(const void *);
typedef void *(*neverc_krt_get_task_cred_fn)(struct task_struct *);

#define _NEVERC_KRT_CRED_CAP_SIZE  8

static neverc_krt_prepare_creds_fn _neverc_krt_prepare_creds;
static neverc_krt_commit_creds_fn  _neverc_krt_commit_creds;
static neverc_krt_abort_creds_fn   _neverc_krt_abort_creds;
static neverc_krt_get_task_cred_fn _neverc_krt_get_task_cred;
static neverc_krt_put_cred_fn _neverc_krt_cred_put;
static int                    _neverc_krt_cred_inited;

static __always_inline int _neverc_krt_cred_field_fits(
	unsigned long size, unsigned long offset, unsigned long width)
{
	return size && offset < size && width <= size - offset;
}

static __always_inline void
_neverc_krt_cred_write_ids(void *cred,
			   const struct neverc_krt_gki_layout *layout,
			   u32 uid, u32 gid)
{
	unsigned char *p = (unsigned char *)cred;

	*(u32 *)(p + layout->cred_uid) = uid;
	*(u32 *)(p + layout->cred_gid) = gid;
	*(u32 *)(p + layout->cred_suid) = uid;
	*(u32 *)(p + layout->cred_sgid) = gid;
	*(u32 *)(p + layout->cred_euid) = uid;
	*(u32 *)(p + layout->cred_egid) = gid;
	*(u32 *)(p + layout->cred_fsuid) = uid;
	*(u32 *)(p + layout->cred_fsgid) = gid;
}

static __always_inline int _neverc_krt_cred_ids_layout_ready(
	const struct neverc_krt_gki_layout *layout)
{
	return layout &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_uid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_gid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_suid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_sgid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_euid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_egid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_fsuid,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size, layout->cred_fsgid,
					   sizeof(u32));
}

static __always_inline int _neverc_krt_cred_caps_layout_ready(
	const struct neverc_krt_gki_layout *layout)
{
	return layout &&
	       _neverc_krt_cred_field_fits(layout->cred_size,
					   layout->cred_securebits,
					   sizeof(u32)) &&
	       _neverc_krt_cred_field_fits(layout->cred_size,
					   layout->cred_cap_inheritable,
					   _NEVERC_KRT_CRED_CAP_SIZE) &&
	       _neverc_krt_cred_field_fits(layout->cred_size,
					   layout->cred_cap_permitted,
					   _NEVERC_KRT_CRED_CAP_SIZE) &&
	       _neverc_krt_cred_field_fits(layout->cred_size,
					   layout->cred_cap_effective,
					   _NEVERC_KRT_CRED_CAP_SIZE) &&
	       _neverc_krt_cred_field_fits(layout->cred_size,
					   layout->cred_cap_bset,
					   _NEVERC_KRT_CRED_CAP_SIZE) &&
	       _neverc_krt_cred_field_fits(layout->cred_size,
					   layout->cred_cap_ambient,
					   _NEVERC_KRT_CRED_CAP_SIZE);
}

static void _neverc_krt_cred_discard(void *cred)
{
	if (cred && _neverc_krt_abort_creds)
		_neverc_krt_abort_creds(cred);
}

static __always_inline unsigned long
_neverc_krt_cred_cap_offset(const struct neverc_krt_gki_layout *layout,
			    int set_type)
{
	switch (set_type) {
	case NEVERC_KRT_CAP_SET_INHERITABLE:
		return layout->cred_cap_inheritable;
	case NEVERC_KRT_CAP_SET_PERMITTED:
		return layout->cred_cap_permitted;
	case NEVERC_KRT_CAP_SET_EFFECTIVE:
		return layout->cred_cap_effective;
	case NEVERC_KRT_CAP_SET_BOUNDING:
		return layout->cred_cap_bset;
	case NEVERC_KRT_CAP_SET_AMBIENT:
		return layout->cred_cap_ambient;
	default:
		return ~0UL;
	}
}

static __always_inline void
_neverc_krt_cred_fill_cap(void *cred, unsigned long offset, u32 value)
{
	u32 *words = (u32 *)((unsigned char *)cred + offset);

	words[0] = value;
	words[1] = value;
}

int neverc_krt_cred_init(void)
{
	if (_neverc_krt_cred_inited) return 0;

	if (neverc_krt_process_init())
		return -1;

	_neverc_krt_prepare_creds =
		(neverc_krt_prepare_creds_fn)NEVERC_KRT_LOOKUP("prepare_creds");
	_neverc_krt_commit_creds =
		(neverc_krt_commit_creds_fn)NEVERC_KRT_LOOKUP("commit_creds");
	_neverc_krt_abort_creds =
		(neverc_krt_abort_creds_fn)NEVERC_KRT_LOOKUP("abort_creds");
	_neverc_krt_get_task_cred =
		(neverc_krt_get_task_cred_fn)NEVERC_KRT_LOOKUP("get_task_cred");
	_neverc_krt_cred_put =
		(neverc_krt_put_cred_fn)NEVERC_KRT_LOOKUP("__put_cred");

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds ||
	    !_neverc_krt_abort_creds)
		return -1;

	_neverc_krt_cred_inited = 1;
	return 0;
}

void *neverc_krt_prepare_creds(void)
{
	if (!_neverc_krt_cred_inited && neverc_krt_cred_init())
		return (void *)0;
	return _neverc_krt_prepare_creds();
}

int neverc_krt_commit_creds(void *cred)
{
	if (!cred)
		return -1;
	if (!_neverc_krt_cred_inited && neverc_krt_cred_init())
		return -1;
	return _neverc_krt_commit_creds(cred);
}

void *neverc_krt_task_get_cred(struct task_struct *task)
{
	if (!task)
		return (void *)0;
	if (!_neverc_krt_cred_inited && neverc_krt_cred_init())
		return (void *)0;
	if (!_neverc_krt_get_task_cred)
		return (void *)0;
	return _neverc_krt_get_task_cred(task);
}

void neverc_krt_task_put_cred(void *cred)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long uid_off;

	if (!cred)
		return;
	if (!_neverc_krt_cred_inited)
		neverc_krt_cred_init();
	if (!_neverc_krt_cred_put)
		return;

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout)
		return;
	uid_off = layout->cred_uid;
	if (uid_off >= sizeof(long)) {
		long *usage = (long *)cred;

		if (__atomic_sub_fetch(usage, 1, __ATOMIC_ACQ_REL) == 0)
			_neverc_krt_cred_put(cred);
	} else {
		int *usage = (int *)cred;

		if (__atomic_sub_fetch(usage, 1, __ATOMIC_ACQ_REL) == 0)
			_neverc_krt_cred_put(cred);
	}
}

int neverc_krt_cred_get_ids(struct task_struct *task,
			    struct neverc_krt_cred_ids *ids)
{
	const struct neverc_krt_gki_layout *layout;
	struct neverc_krt_cred_ids value = { 0 };
	const void *cred;
	unsigned long cred_val;
	const unsigned char *p;
	int result = -1;

	if (!ids)
		return -1;
	__builtin_memset(ids, 0, sizeof(*ids));
	if (!task || !_neverc_krt_kernel_pointer_is_valid(task) ||
	    !_neverc_krt_mem_nofault_available())
		return -1;

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!layout || !_neverc_krt_cred_field_fits(
			layout->task_size, layout->task_real_cred,
			sizeof(cred_val)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_uid,
			sizeof(value.uid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_gid,
			sizeof(value.gid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_suid,
			sizeof(value.suid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_sgid,
			sizeof(value.sgid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_euid,
			sizeof(value.euid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_egid,
			sizeof(value.egid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_fsuid,
			sizeof(value.fsuid)) ||
	    !_neverc_krt_cred_field_fits(
			layout->cred_size, layout->cred_fsgid,
			sizeof(value.fsgid)) ||
	    (unsigned long)task > ~0UL - layout->task_real_cred ||
	    _neverc_krt_rcu_read_begin())
		return -1;
	if (neverc_krt_mem_read(&cred_val,
			(const char *)task + layout->task_real_cred,
			sizeof(cred_val)))
		goto out_unlock;
	cred = (const void *)cred_val;
	if (!_neverc_krt_kernel_pointer_is_valid(cred))
		goto out_unlock;

	p = (const unsigned char *)cred;
	if (neverc_krt_mem_read(&value.uid, p + layout->cred_uid,
				sizeof(value.uid)) ||
	    neverc_krt_mem_read(&value.gid, p + layout->cred_gid,
				sizeof(value.gid)) ||
	    neverc_krt_mem_read(&value.suid, p + layout->cred_suid,
				sizeof(value.suid)) ||
	    neverc_krt_mem_read(&value.sgid, p + layout->cred_sgid,
				sizeof(value.sgid)) ||
	    neverc_krt_mem_read(&value.euid, p + layout->cred_euid,
				sizeof(value.euid)) ||
	    neverc_krt_mem_read(&value.egid, p + layout->cred_egid,
				sizeof(value.egid)) ||
	    neverc_krt_mem_read(&value.fsuid, p + layout->cred_fsuid,
				sizeof(value.fsuid)) ||
	    neverc_krt_mem_read(&value.fsgid, p + layout->cred_fsgid,
				sizeof(value.fsgid)))
		goto out_unlock;
	*ids = value;
	result = 0;

out_unlock:
	_neverc_krt_rcu_read_end();
	return result;
}

int neverc_krt_cred_set_uid0(void)
{
	const struct neverc_krt_gki_layout *layout;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!_neverc_krt_cred_ids_layout_ready(layout) ||
	    !_neverc_krt_cred_caps_layout_ready(layout)) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}

	/*
	 * Raw writes are safe here: prepare_creds() returns a freshly
	 * allocated, writable kernel-heap copy of the credential struct.
	 * No page-table manipulation or mem_write_protected needed.
	 */
	_neverc_krt_cred_write_ids(cred, layout, 0, 0);
	*(u32 *)((unsigned char *)cred + layout->cred_securebits) = 0;
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_inheritable, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_permitted, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_effective, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_bset, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_ambient, 0xFFFFFFFFU);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_set_uid(u32 uid, u32 gid)
{
	const struct neverc_krt_gki_layout *layout;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_proven_gki_layout(
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK);
	if (!_neverc_krt_cred_ids_layout_ready(layout)) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	_neverc_krt_cred_write_ids(cred, layout, uid, gid);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_set_caps_full(void)
{
	const struct neverc_krt_gki_layout *layout;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!_neverc_krt_cred_caps_layout_ready(layout)) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_inheritable, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_permitted, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_effective, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_bset, 0xFFFFFFFFU);
	_neverc_krt_cred_fill_cap(
		cred, layout->cred_cap_ambient, 0xFFFFFFFFU);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_set_cap(int cap, int set_type)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long set_off;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;
	if (cap < 0 || cap > 63 || set_type < 0 || set_type > 4)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!_neverc_krt_cred_caps_layout_ready(layout)) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	set_off = _neverc_krt_cred_cap_offset(layout, set_type);
	if (set_off == ~0UL ||
	    !_neverc_krt_cred_field_fits(layout->cred_size, set_off + 4UL,
					 sizeof(u32))) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	unsigned char *p = (unsigned char *)cred;

	int word = cap / 32;
	int bit = cap % 32;
	u32 *cap_word = (u32 *)(p + set_off + word * 4);
	*cap_word |= (1U << bit);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_clear_cap(int cap, int set_type)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long set_off;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;
	if (cap < 0 || cap > 63 || set_type < 0 || set_type > 4)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!_neverc_krt_cred_caps_layout_ready(layout)) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	set_off = _neverc_krt_cred_cap_offset(layout, set_type);
	if (set_off == ~0UL ||
	    !_neverc_krt_cred_field_fits(layout->cred_size, set_off + 4UL,
					 sizeof(u32))) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	unsigned char *p = (unsigned char *)cred;

	int word = cap / 32;
	int bit = cap % 32;
	u32 *cap_word = (u32 *)(p + set_off + word * 4);
	*cap_word &= ~(1U << bit);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_has_cap(struct task_struct *task, int cap, int set_type)
{
	const struct neverc_krt_gki_layout *layout;
	unsigned long set_off;
	const void *cred;
	const unsigned char *p;
	unsigned long cred_val;

	if (!task || cap < 0 || cap > 63 || set_type < 0 || set_type > 4 ||
	    !_neverc_krt_kernel_pointer_is_valid(task) ||
	    !_neverc_krt_mem_nofault_available())
		return -1;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!_neverc_krt_cred_caps_layout_ready(layout) ||
	    !_neverc_krt_cred_field_fits(layout->task_size,
					 layout->task_real_cred,
					 sizeof(cred_val)) ||
	    _neverc_krt_rcu_read_begin())
		return -1;
	if (neverc_krt_mem_read(&cred_val,
			(const char *)task + layout->task_real_cred,
			sizeof(cred_val)))
		goto out_unlock;
	cred = (const void *)cred_val;
	if (!_neverc_krt_kernel_pointer_is_valid(cred))
		goto out_unlock;

	p = (const unsigned char *)cred;
	set_off = _neverc_krt_cred_cap_offset(layout, set_type);
	if (set_off == ~0UL ||
	    !_neverc_krt_cred_field_fits(layout->cred_size, set_off + 4UL,
					 sizeof(u32)))
		goto out_unlock;

	int word = cap / 32;
	int bit = cap % 32;
	u32 cap_val;
	if (neverc_krt_mem_read(&cap_val, p + set_off + word * 4, 4))
		goto out_unlock;
	_neverc_krt_rcu_read_end();
	return (cap_val >> bit) & 1;

out_unlock:
	_neverc_krt_rcu_read_end();
	return -1;
}

int neverc_krt_cred_clear_securebits(void)
{
	const struct neverc_krt_gki_layout *layout;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_proven_gki_layout(NEVERC_KRT_LAYOUT_CERT_FULL);
	if (!_neverc_krt_cred_caps_layout_ready(layout)) {
		_neverc_krt_cred_discard(cred);
		return -1;
	}
	*(u32 *)((unsigned char *)cred + layout->cred_securebits) = 0;

	return _neverc_krt_commit_creds(cred);
}

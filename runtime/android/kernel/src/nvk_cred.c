/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal typedefs ---- */

typedef void *(*neverc_krt_prepare_creds_fn)(void);
typedef int   (*neverc_krt_commit_creds_fn)(void *);
typedef void  (*neverc_krt_put_cred_fn)(const void *);
typedef void *(*neverc_krt_get_task_cred_fn)(struct task_struct *);

#define _NEVERC_KRT_CRED_CAP_SIZE  8

static neverc_krt_prepare_creds_fn _neverc_krt_prepare_creds;
static neverc_krt_commit_creds_fn  _neverc_krt_commit_creds;
static neverc_krt_get_task_cred_fn _neverc_krt_get_task_cred;
static neverc_krt_put_cred_fn _neverc_krt_cred_put;
static int                    _neverc_krt_cred_inited;

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

	neverc_krt_process_init();

	_neverc_krt_prepare_creds =
		(neverc_krt_prepare_creds_fn)NEVERC_KRT_LOOKUP("prepare_creds");
	_neverc_krt_commit_creds =
		(neverc_krt_commit_creds_fn)NEVERC_KRT_LOOKUP("commit_creds");
	_neverc_krt_get_task_cred =
		(neverc_krt_get_task_cred_fn)NEVERC_KRT_LOOKUP("get_task_cred");
	_neverc_krt_cred_put =
		(neverc_krt_put_cred_fn)NEVERC_KRT_LOOKUP("__put_cred");

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
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

	layout = _neverc_krt_get_gki_layout();
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
	const void *cred;
	unsigned long cred_val;
	const unsigned char *p;

	if (!task || !ids) return -1;

	layout = _neverc_krt_get_gki_layout();
	if (neverc_krt_mem_read(&cred_val,
			(const char *)task + layout->task_real_cred,
			sizeof(cred_val)))
		return -1;
	cred = (const void *)cred_val;
	if (!cred) return -1;

	p = (const unsigned char *)cred;
	if (neverc_krt_mem_read(&ids->uid, p + layout->cred_uid,
				sizeof(ids->uid)) ||
	    neverc_krt_mem_read(&ids->gid, p + layout->cred_gid,
				sizeof(ids->gid)) ||
	    neverc_krt_mem_read(&ids->suid, p + layout->cred_suid,
				sizeof(ids->suid)) ||
	    neverc_krt_mem_read(&ids->sgid, p + layout->cred_sgid,
				sizeof(ids->sgid)) ||
	    neverc_krt_mem_read(&ids->euid, p + layout->cred_euid,
				sizeof(ids->euid)) ||
	    neverc_krt_mem_read(&ids->egid, p + layout->cred_egid,
				sizeof(ids->egid)) ||
	    neverc_krt_mem_read(&ids->fsuid, p + layout->cred_fsuid,
				sizeof(ids->fsuid)) ||
	    neverc_krt_mem_read(&ids->fsgid, p + layout->cred_fsgid,
				sizeof(ids->fsgid)))
		return -1;
	return 0;
}

int neverc_krt_cred_set_uid0(void)
{
	const struct neverc_krt_gki_layout *layout;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_gki_layout();

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

	layout = _neverc_krt_get_gki_layout();
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

	layout = _neverc_krt_get_gki_layout();
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

	layout = _neverc_krt_get_gki_layout();
	set_off = _neverc_krt_cred_cap_offset(layout, set_type);
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

	layout = _neverc_krt_get_gki_layout();
	set_off = _neverc_krt_cred_cap_offset(layout, set_type);
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

	if (!task || cap < 0 || cap > 63 || set_type < 0 || set_type > 4)
		return -1;

	layout = _neverc_krt_get_gki_layout();
	if (neverc_krt_mem_read(&cred_val,
			(const char *)task + layout->task_real_cred,
			sizeof(cred_val)))
		return -1;
	cred = (const void *)cred_val;
	if (!cred) return -1;

	p = (const unsigned char *)cred;
	set_off = _neverc_krt_cred_cap_offset(layout, set_type);

	int word = cap / 32;
	int bit = cap % 32;
	u32 cap_val;
	if (neverc_krt_mem_read(&cap_val, p + set_off + word * 4, 4))
		return -1;
	return (cap_val >> bit) & 1;
}

int neverc_krt_cred_clear_securebits(void)
{
	const struct neverc_krt_gki_layout *layout;
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	layout = _neverc_krt_get_gki_layout();
	*(u32 *)((unsigned char *)cred + layout->cred_securebits) = 0;

	return _neverc_krt_commit_creds(cred);
}


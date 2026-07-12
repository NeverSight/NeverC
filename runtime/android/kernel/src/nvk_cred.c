/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

unsigned long _neverc_krt_off_cred = 0;
unsigned long _neverc_krt_off_uid = 0;

/* ---- internal typedefs ---- */

typedef void  (*neverc_krt_put_cred_fn)(const void *);

#define _NEVERC_KRT_CRED_CAP_SIZE  8

static neverc_krt_put_cred_fn _neverc_krt_cred_put;
static int                    _neverc_krt_cred_inited;
static unsigned long          _neverc_krt_cred_cap_off;
static unsigned long          _neverc_krt_cred_sb_off;

int neverc_krt_cred_init(void)
{
	if (_neverc_krt_cred_inited) return 0;

	neverc_krt_process_init();
	_neverc_krt_cred_find_uid_offset();

	_neverc_krt_cred_put =
		(neverc_krt_put_cred_fn)NEVERC_KRT_LOOKUP("put_cred");
	if (!_neverc_krt_cred_put)
		_neverc_krt_cred_put =
			(neverc_krt_put_cred_fn)NEVERC_KRT_LOOKUP("__put_cred");

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	_neverc_krt_cred_inited = 1;
	return 0;
}

int _neverc_krt_cred_find_uid_offset(void)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_gki_layout();

	__atomic_store_n(&_neverc_krt_off_cred, layout->task_real_cred,
			 __ATOMIC_RELEASE);
	__atomic_store_n(&_neverc_krt_off_uid, layout->cred_uid,
			 __ATOMIC_RELEASE);
	_neverc_krt_cred_sb_off =
		layout->cred_securebits - layout->cred_uid;
	_neverc_krt_cred_cap_off =
		layout->cred_cap_inheritable - layout->cred_uid;
	return 0;
}

int neverc_krt_cred_get_ids(struct task_struct *task,
			    struct neverc_krt_cred_ids *ids)
{
	const void *cred;
	const unsigned char *p;

	if (!task || !ids) return -1;

	if (!__atomic_load_n(&_neverc_krt_off_cred, __ATOMIC_ACQUIRE))
		_neverc_krt_cred_find_uid_offset();

	if (!_neverc_krt_off_cred) return -1;

	unsigned long cred_val;
	if (neverc_krt_mem_read(&cred_val,
			(void *)((unsigned long)task +
				 _neverc_krt_off_cred), 8))
		return -1;
	cred = (const void *)cred_val;
	if (!cred) return -1;

	p = (const unsigned char *)cred;
	unsigned long base = _neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base();

	u32 raw[8];
	if (neverc_krt_mem_read(raw, p + base, sizeof(raw)))
		return -1;
	ids->uid   = raw[0]; ids->gid   = raw[1];
	ids->suid  = raw[2]; ids->sgid  = raw[3];
	ids->euid  = raw[4]; ids->egid  = raw[5];
	ids->fsuid = raw[6]; ids->fsgid = raw[7];
	return 0;
}

int neverc_krt_cred_set_uid0(void)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();

	/*
	 * Raw writes are safe here: prepare_creds() returns a freshly
	 * allocated, writable kernel-heap copy of the credential struct.
	 * No page-table manipulation or mem_write_protected needed.
	 */
	unsigned char *p = (unsigned char *)cred;
	unsigned long base = _neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base();

	for (int i = 0; i < 8; i++)
		*(u32 *)(p + base + i * 4) = 0;

	unsigned long sb = base + _neverc_krt_cred_sb_off;
	*(u32 *)(p + sb) = 0;

	unsigned long cap_off = base + _neverc_krt_cred_cap_off;
	for (int i = 0; i < 10; i++)
		*(u32 *)(p + cap_off + i * 4) = 0xFFFFFFFFU;

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_set_uid(u32 uid, u32 gid)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long base = _neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base();

	*(u32 *)(p + base + 0)  = uid;   /* uid */
	*(u32 *)(p + base + 4)  = gid;   /* gid */
	*(u32 *)(p + base + 8)  = uid;   /* suid */
	*(u32 *)(p + base + 12) = gid;   /* sgid */
	*(u32 *)(p + base + 16) = uid;   /* euid */
	*(u32 *)(p + base + 20) = gid;   /* egid */
	*(u32 *)(p + base + 24) = uid;   /* fsuid */
	*(u32 *)(p + base + 28) = gid;   /* fsgid */

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_set_caps_full(void)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();
	_neverc_krt_cred_probe_cap_offset(cred);

	unsigned char *p = (unsigned char *)cred;
	unsigned long cap_off = (_neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base())
				+ _neverc_krt_cred_cap_off;

	for (int i = 0; i < 10; i++)
		*(u32 *)(p + cap_off + i * 4) = 0xFFFFFFFFU;

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_set_cap(int cap, int set_type)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;
	if (cap < 0 || cap > 63 || set_type < 0 || set_type > 4)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();
	_neverc_krt_cred_probe_cap_offset(cred);

	unsigned char *p = (unsigned char *)cred;
	unsigned long cap_base = (_neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base())
				 + _neverc_krt_cred_cap_off;
	unsigned long set_off = cap_base + set_type * _NEVERC_KRT_CRED_CAP_SIZE;

	int word = cap / 32;
	int bit = cap % 32;
	u32 *cap_word = (u32 *)(p + set_off + word * 4);
	*cap_word |= (1U << bit);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_clear_cap(int cap, int set_type)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;
	if (cap < 0 || cap > 63 || set_type < 0 || set_type > 4)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();
	_neverc_krt_cred_probe_cap_offset(cred);

	unsigned char *p = (unsigned char *)cred;
	unsigned long cap_base = (_neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base())
				 + _neverc_krt_cred_cap_off;
	unsigned long set_off = cap_base + set_type * _NEVERC_KRT_CRED_CAP_SIZE;

	int word = cap / 32;
	int bit = cap % 32;
	u32 *cap_word = (u32 *)(p + set_off + word * 4);
	*cap_word &= ~(1U << bit);

	return _neverc_krt_commit_creds(cred);
}

int neverc_krt_cred_has_cap(struct task_struct *task, int cap, int set_type)
{
	const void *cred;
	const unsigned char *p;

	if (!task || cap < 0 || cap > 63 || set_type < 0 || set_type > 4)
		return -1;

	if (!_neverc_krt_off_cred) return -1;

	unsigned long cred_val;
	if (neverc_krt_mem_read(&cred_val,
			(void *)((unsigned long)task +
				 _neverc_krt_off_cred), 8))
		return -1;
	cred = (const void *)cred_val;
	if (!cred) return -1;

	p = (const unsigned char *)cred;
	if (!_neverc_krt_cred_cap_off)
		_neverc_krt_cred_probe_cap_offset(cred);
	unsigned long cap_base = (_neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base())
				 + _neverc_krt_cred_cap_off;
	unsigned long set_off = cap_base + set_type * _NEVERC_KRT_CRED_CAP_SIZE;

	int word = cap / 32;
	int bit = cap % 32;
	u32 cap_val;
	if (neverc_krt_mem_read(&cap_val, p + set_off + word * 4, 4))
		return -1;
	return (cap_val >> bit) & 1;
}

int neverc_krt_cred_clear_securebits(void)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();
	_neverc_krt_cred_probe_cap_offset(cred);

	unsigned char *p = (unsigned char *)cred;
	unsigned long sb_off = (_neverc_krt_off_uid ? _neverc_krt_off_uid : _neverc_krt_cred_uid_base())
			       + _neverc_krt_cred_sb_off;
	*(u32 *)(p + sb_off) = 0;

	return _neverc_krt_commit_creds(cred);
}


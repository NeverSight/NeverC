/* SPDX-License-Identifier: GPL-2.0 */
/* neverc_krt_cred.c — implementations extracted from neverc_krt_cred.h. */
#include <nvk.h>

void _neverc_krt_cred_probe_cap_offset(const void *cred)
{
	if (_neverc_krt_cred_cap_off) return;

	const unsigned char *p = (const unsigned char *)cred;
	unsigned long uid_base = _neverc_krt_off_uid ? _neverc_krt_off_uid
						  : _neverc_krt_cred_uid_base();

	
	unsigned long scan_start = uid_base + 32;
	unsigned long scan_end = uid_base + 128;
	unsigned long off;

	for (off = scan_start; off < scan_end; off += 4) {
		int plausible = 1;
		int j;
		for (j = 0; j < 5; j++) {
			u32 lo, hi;
			if (neverc_krt_mem_read(&lo, p + off + j * 8, 4)) {
				plausible = 0; break;
			}
			if (neverc_krt_mem_read(&hi, p + off + j * 8 + 4, 4)) {
				plausible = 0; break;
			}
			if (lo == 0 && hi == 0 && j < 3) {
				plausible = 0; break;
			}
		}
		if (plausible) {
			_neverc_krt_cred_cap_off = off - uid_base;
			_neverc_krt_cred_sb_off = _neverc_krt_cred_cap_off - 4;
			return;
		}
	}

	_neverc_krt_cred_cap_off = 36;
	_neverc_krt_cred_sb_off = 32;
}

int neverc_krt_cred_init(void)
{
	if (_neverc_krt_cred_inited) return 0;

	if (!_neverc_krt_proc_inited)
		neverc_krt_process_init();

	_neverc_krt_cred_get =
		(neverc_krt_get_cred_fn)NEVERC_KRT_LOOKUP("get_cred");
	_neverc_krt_cred_put =
		(neverc_krt_put_cred_fn)NEVERC_KRT_LOOKUP("put_cred");

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	_neverc_krt_cred_inited = 1;
	return 0;
}

int _neverc_krt_cred_find_uid_offset(void)
{
	if (_neverc_krt_off_uid) return 0;

	/*
	 * struct cred layout change:
	 *   5.10-6.1: atomic_t   usage (4 bytes) → uid at offset 4
	 *   6.6+:     atomic_long_t usage (8 bytes) → uid at offset 8
	 * Scanning must start AFTER the usage field to avoid a false
	 * match when the upper half of an 8-byte refcount is zero and
	 * the current process is root (all UIDs/GIDs are 0).
	 */
	int kv = __atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE);
	unsigned long scan_start = (kv >= 606) ? 8 : 4;

	const void *cred = (void *)0;
	unsigned long task;
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));

	if (_neverc_krt_off_cred) {
		unsigned long cv;
		if (neverc_krt_mem_read(&cv,
				(void *)((unsigned long)task +
					 _neverc_krt_off_cred), 8))
			cv = 0;
		cred = (const void *)cv;
	} else if (_neverc_krt_get_task_cred) {
		cred = _neverc_krt_get_task_cred((struct task_struct *)task);
	}

	if (!cred) {
		_neverc_krt_off_uid = scan_start;
		return 0;
	}

	const unsigned char *p = (const unsigned char *)cred;

	for (unsigned long i = scan_start; i + 24 < 128; i += 4) {
		u32 ids[6];
		if (neverc_krt_mem_read(ids, p + i, sizeof(ids)))
			continue;
		u32 uid = ids[0], gid = ids[1], suid = ids[2];
		u32 sgid = ids[3], euid = ids[4], egid = ids[5];

		if (uid == suid && suid == euid &&
		    gid == sgid && sgid == egid &&
		    uid < 65536 && gid < 65536) {
			_neverc_krt_off_uid = i;
			break;
		}
	}

	if (_neverc_krt_cred_put && _neverc_krt_get_task_cred)
		_neverc_krt_cred_put(cred);

	if (!_neverc_krt_off_uid)
		_neverc_krt_off_uid = scan_start;
	return 0;
}

int neverc_krt_cred_get_ids(struct task_struct *task,
			    struct neverc_krt_cred_ids *ids)
{
	const void *cred;
	const unsigned char *p;

	if (!task || !ids) return -1;

	if (!__atomic_load_n(&_neverc_krt_off_cred, __ATOMIC_ACQUIRE)) {
		const unsigned char *tp = (const unsigned char *)task;
		unsigned long uid_off = _neverc_krt_cred_uid_base();
		unsigned long i;
		for (i = 0x400; i < 0xE00; i += 8) {
			unsigned long v1, v2;
			if (neverc_krt_mem_read(&v1, tp + i, 8)) continue;
			if (neverc_krt_mem_read(&v2, tp + i + 8, 8)) continue;
			if (v1 <= 0xFFFF000000000000UL ||
			    v1 >= 0xFFFFFFFFFFFFF000UL)
				continue;
			if (v2 <= 0xFFFF000000000000UL ||
			    v2 >= 0xFFFFFFFFFFFFF000UL)
				continue;

			u32 refcnt;
			if (neverc_krt_mem_read(&refcnt, (void *)v1, 4))
				continue;
			if (refcnt < 1 || refcnt > 10000)
				continue;

			u32 uid_buf[8];
			if (neverc_krt_mem_read(uid_buf,
					(void *)(v1 + uid_off),
					sizeof(uid_buf)))
				continue;
			int match = 1;
			for (int j = 0; j < 8; j++) {
				if (uid_buf[j] > 65535) { match = 0; break; }
			}
			if (match) {
				__atomic_store_n(&_neverc_krt_off_cred, i,
						 __ATOMIC_RELEASE);
				break;
			}
		}
	}

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

int neverc_krt_cred_set_root(void)
{
	void *cred;

	if (!_neverc_krt_prepare_creds || !_neverc_krt_commit_creds)
		return -1;

	cred = _neverc_krt_prepare_creds();
	if (!cred) return -1;

	_neverc_krt_cred_find_uid_offset();
	_neverc_krt_cred_probe_cap_offset(cred);

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


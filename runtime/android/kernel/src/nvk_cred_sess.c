/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

/* ---- internal variables ---- */

static struct neverc_krt_cred_sess _neverc_krt_cred_sess;

/* ---- internal helpers ---- */

static __always_inline void _neverc_krt_cred_sess_lock(void)
{
	while (__atomic_exchange_n(&_neverc_krt_cred_sess.lock, 1,
				   __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _neverc_krt_cred_sess_unlock(void)
{
	__atomic_store_n(&_neverc_krt_cred_sess.lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

static __always_inline int
_neverc_krt_cred_sess_expired(struct neverc_krt_cred_sess_entry *e)
{
	if (!e->expire_ts) return 0;
	return neverc_krt_arch_counter() > e->expire_ts;
}

void neverc_krt_cred_sess_init(u64 sess_key)
{
	unsigned char *p = (unsigned char *)&_neverc_krt_cred_sess;
	unsigned long i;
	for (i = 0; i < sizeof(_neverc_krt_cred_sess); i++) p[i] = 0;
	_neverc_krt_cred_sess.sess_key = sess_key;
}

int neverc_krt_cred_sess_allow(u32 uid, u32 flags, u64 token, u64 ttl_ns)
{
	int i, slot = -1;

	_neverc_krt_cred_sess_lock();

	for (i = 0; i < _neverc_krt_cred_sess.count; i++) {
		if (_neverc_krt_cred_sess.entries[i].uid == uid &&
		    _neverc_krt_cred_sess.entries[i].active) {
			_neverc_krt_cred_sess.entries[i].flags |= flags;
			if (token) _neverc_krt_cred_sess.entries[i].token = token;
			_neverc_krt_cred_sess_unlock();
			return 0;
		}
	}

	if (_neverc_krt_cred_sess.count >= NEVERC_KRT_CRED_SESS_MAX) {
		for (i = 0; i < _neverc_krt_cred_sess.count; i++) {
			if (!_neverc_krt_cred_sess.entries[i].active) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			_neverc_krt_cred_sess_unlock();
			return -1;
		}
	} else {
		slot = _neverc_krt_cred_sess.count++;
	}

	_neverc_krt_cred_sess.entries[slot].uid = uid;
	_neverc_krt_cred_sess.entries[slot].flags = flags;
	_neverc_krt_cred_sess.entries[slot].token = token;
	_neverc_krt_cred_sess.entries[slot].expire_ts = 0;
	if (ttl_ns > 0) {
		u64 freq = (u64)neverc_krt_arch_counter_freq();
		u64 now = neverc_krt_arch_counter();
		_neverc_krt_cred_sess.entries[slot].expire_ts =
			now + ttl_ns * freq / 1000000000ULL;
	}
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_neverc_krt_cred_sess.entries[slot].active, 1);

	_neverc_krt_cred_sess_unlock();
	return 0;
}

int neverc_krt_cred_sess_revoke(u32 uid)
{
	int i, found = 0;

	_neverc_krt_cred_sess_lock();
	for (i = 0; i < _neverc_krt_cred_sess.count; i++) {
		if (_neverc_krt_cred_sess.entries[i].uid == uid &&
		    _neverc_krt_cred_sess.entries[i].active) {
			WRITE_ONCE(_neverc_krt_cred_sess.entries[i].active, 0);
			found = 1;
		}
	}
	_neverc_krt_cred_sess_unlock();
	return found ? 0 : -1;
}

void neverc_krt_cred_sess_clear(void)
{
	int i;
	_neverc_krt_cred_sess_lock();
	for (i = 0; i < _neverc_krt_cred_sess.count; i++)
		WRITE_ONCE(_neverc_krt_cred_sess.entries[i].active, 0);
	_neverc_krt_cred_sess.count = 0;
	_neverc_krt_cred_sess_unlock();
}

u32 neverc_krt_cred_sess_query(u32 uid)
{
	int i;
	u32 flags = 0;

	_neverc_krt_cred_sess_lock();
	for (i = 0; i < _neverc_krt_cred_sess.count; i++) {
		if (!_neverc_krt_cred_sess.entries[i].active) continue;
		if (_neverc_krt_cred_sess.entries[i].uid != uid) continue;
		if (_neverc_krt_cred_sess_expired(
			    &_neverc_krt_cred_sess.entries[i])) {
			WRITE_ONCE(_neverc_krt_cred_sess.entries[i].active, 0);
			continue;
		}
		flags = _neverc_krt_cred_sess.entries[i].flags;
		break;
	}
	_neverc_krt_cred_sess_unlock();

	if (flags)
		__atomic_fetch_add(&_neverc_krt_cred_sess.allow_hits, 1,
				   __ATOMIC_RELAXED);
	else
		__atomic_fetch_add(&_neverc_krt_cred_sess.deny_hits, 1,
				   __ATOMIC_RELAXED);
	return flags;
}

int neverc_krt_cred_sess_check_token(u32 uid, u64 token)
{
	u64 key = READ_ONCE(_neverc_krt_cred_sess.sess_key);
	if (key != 0 && token == key)
		return 1;

	int i;
	_neverc_krt_cred_sess_lock();
	for (i = 0; i < _neverc_krt_cred_sess.count; i++) {
		if (!_neverc_krt_cred_sess.entries[i].active) continue;
		if (_neverc_krt_cred_sess.entries[i].uid != uid) continue;
		if (_neverc_krt_cred_sess_expired(
			    &_neverc_krt_cred_sess.entries[i])) {
			WRITE_ONCE(_neverc_krt_cred_sess.entries[i].active, 0);
			_neverc_krt_cred_sess_unlock();
			return 0;
		}
		if (_neverc_krt_cred_sess.entries[i].token == token) {
			_neverc_krt_cred_sess_unlock();
			return 1;
		}
		break;
	}
	_neverc_krt_cred_sess_unlock();
	return 0;
}

int neverc_krt_cred_sess_apply(u32 uid, u64 token)
{
	u32 flags = 0;

	if (!neverc_krt_cred_sess_check_token(uid, token))
		return -1;

	flags = neverc_krt_cred_sess_query(uid);
	if (!flags) return -2;

	if (flags & NEVERC_KRT_CRED_SESS_FLAG_UID0) {
		int ret = neverc_krt_cred_set_uid0();
		if (ret) return ret;
	}

	if (flags & NEVERC_KRT_CRED_SESS_FLAG_CAPS)
		neverc_krt_cred_set_caps_full();

	return 0;
}

int neverc_krt_cred_sess_reset(void)
{
	return neverc_krt_cred_set_uid(2000, 2000);
}

int neverc_krt_cred_sess_active_count(void)
{
	int i, count = 0;
	_neverc_krt_cred_sess_lock();
	for (i = 0; i < _neverc_krt_cred_sess.count; i++) {
		if (_neverc_krt_cred_sess.entries[i].active &&
		    !_neverc_krt_cred_sess_expired(
			    &_neverc_krt_cred_sess.entries[i]))
			count++;
	}
	_neverc_krt_cred_sess_unlock();
	return count;
}

void neverc_krt_cred_sess_get_stats(struct neverc_krt_cred_sess_stats *out)
{
	if (!out) return;
	out->total_allows = __atomic_load_n(&_neverc_krt_cred_sess.allow_hits,
					    __ATOMIC_RELAXED);
	out->total_denies = __atomic_load_n(&_neverc_krt_cred_sess.deny_hits,
					    __ATOMIC_RELAXED);
	out->active = neverc_krt_cred_sess_active_count();
}

int neverc_krt_cred_set_ids_pid(int pid, u32 target_uid, u32 target_gid)
{
	const struct neverc_krt_gki_layout *layout;
	struct task_struct *task;
	unsigned long cred_raw;
	const void *cred;
	unsigned long cred_addr;
	unsigned long base;
	unsigned long eff_cred_ptr;
	unsigned long eff_off;
	unsigned long eff_addr;
	u32 ids[8] = { target_uid, target_gid, target_uid, target_gid,
		       target_uid, target_gid, target_uid, target_gid };
	int ret;

	layout = _neverc_krt_get_gki_layout();
	base = layout->cred_uid;
	if (layout->cred_gid != base + 4 ||
	    layout->cred_suid != base + 8 ||
	    layout->cred_sgid != base + 12 ||
	    layout->cred_euid != base + 16 ||
	    layout->cred_egid != base + 20 ||
	    layout->cred_fsuid != base + 24 ||
	    layout->cred_fsgid != base + 28)
		return -2;

	task = neverc_krt_find_task(pid);
	if (!task)
		return -1;

	if (neverc_krt_mem_read(&cred_raw,
			(void *)((unsigned long)task +
				 layout->task_real_cred),
			sizeof(cred_raw))) {
		ret = -3;
		goto out;
	}
	cred = (const void *)cred_raw;
	if (!cred) {
		ret = -3;
		goto out;
	}

	cred_addr = (unsigned long)cred & ~(0xFFUL << 56);
	if (cred_addr < 0xFFFF000000000000UL ||
	    cred_addr >= 0xFFFFFFFFFFFFF000UL) {
		ret = -4;
		goto out;
	}

	ret = neverc_krt_mem_write_protected(cred_addr + base, ids, sizeof(ids));
	if (ret)
		goto out;

	/*
	 * Also update the effective credential object when it differs from
	 * real_cred.  Both task offsets come from the selected GKI manifest;
	 * do not infer their relationship from a particular kernel version.
	 */
	eff_off = layout->task_cred;
	if (neverc_krt_mem_read(&eff_cred_ptr,
			(void *)((unsigned long)task + eff_off), 8)) {
		ret = 0;
		goto out;
	}
	eff_addr = eff_cred_ptr & ~(0xFFUL << 56);
	if (eff_cred_ptr && eff_addr != cred_addr &&
	    eff_addr >= 0xFFFF000000000000UL &&
	    eff_addr < 0xFFFFFFFFFFFFF000UL) {
		ret = neverc_krt_mem_write_protected(eff_addr + base,
						     ids, sizeof(ids));
		if (ret)
			goto out;
	}
	ret = 0;

out:
	neverc_krt_put_task(task);
	return ret;
}

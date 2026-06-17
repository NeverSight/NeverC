/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

/* ---- internal variables ---- */

struct neverc_krt_su_manager _neverc_krt_su;

/* ---- internal helpers ---- */

static __always_inline void _neverc_krt_su_lock(void)
{
	while (__atomic_exchange_n(&_neverc_krt_su.lock, 1, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _neverc_krt_su_unlock(void)
{
	__atomic_store_n(&_neverc_krt_su.lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

static __always_inline int _neverc_krt_su_expired(struct neverc_krt_su_grant *g)
{
	u64 now;
	if (!g->expire_ts) return 0;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(now));
	return now > g->expire_ts;
}

void neverc_krt_su_init(u64 master_key)
{
	unsigned char *p = (unsigned char *)&_neverc_krt_su;
	unsigned long i;
	for (i = 0; i < sizeof(_neverc_krt_su); i++) p[i] = 0;
	_neverc_krt_su.master_key = master_key;
}

int neverc_krt_su_grant(u32 uid, u32 flags, u64 token, u64 ttl_ns)
{
	int i, slot = -1;

	_neverc_krt_su_lock();

	for (i = 0; i < _neverc_krt_su.count; i++) {
		if (_neverc_krt_su.grants[i].uid == uid &&
		    _neverc_krt_su.grants[i].active) {
			_neverc_krt_su.grants[i].flags |= flags;
			if (token) _neverc_krt_su.grants[i].token = token;
			_neverc_krt_su_unlock();
			return 0;
		}
	}

	if (_neverc_krt_su.count >= NEVERC_KRT_SU_MAX_GRANTS) {
		for (i = 0; i < _neverc_krt_su.count; i++) {
			if (!_neverc_krt_su.grants[i].active) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			_neverc_krt_su_unlock();
			return -1;
		}
	} else {
		slot = _neverc_krt_su.count++;
	}

	_neverc_krt_su.grants[slot].uid = uid;
	_neverc_krt_su.grants[slot].flags = flags;
	_neverc_krt_su.grants[slot].token = token;
	_neverc_krt_su.grants[slot].expire_ts = 0;
	if (ttl_ns > 0) {
		u64 freq, now;
		__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
		__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(now));
		_neverc_krt_su.grants[slot].expire_ts =
			now + ttl_ns * freq / 1000000000ULL;
	}
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_neverc_krt_su.grants[slot].active, 1);

	_neverc_krt_su_unlock();
	return 0;
}

int neverc_krt_su_revoke(u32 uid)
{
	int i, found = 0;

	_neverc_krt_su_lock();
	for (i = 0; i < _neverc_krt_su.count; i++) {
		if (_neverc_krt_su.grants[i].uid == uid &&
		    _neverc_krt_su.grants[i].active) {
			WRITE_ONCE(_neverc_krt_su.grants[i].active, 0);
			found = 1;
		}
	}
	_neverc_krt_su_unlock();
	return found ? 0 : -1;
}

void neverc_krt_su_revoke_all(void)
{
	int i;
	_neverc_krt_su_lock();
	for (i = 0; i < _neverc_krt_su.count; i++)
		WRITE_ONCE(_neverc_krt_su.grants[i].active, 0);
	_neverc_krt_su.count = 0;
	_neverc_krt_su_unlock();
}

u32 neverc_krt_su_check(u32 uid)
{
	int i;
	u32 flags = 0;

	_neverc_krt_su_lock();
	for (i = 0; i < _neverc_krt_su.count; i++) {
		if (!_neverc_krt_su.grants[i].active) continue;
		if (_neverc_krt_su.grants[i].uid != uid) continue;
		if (_neverc_krt_su_expired(&_neverc_krt_su.grants[i])) {
			WRITE_ONCE(_neverc_krt_su.grants[i].active, 0);
			continue;
		}
		flags = _neverc_krt_su.grants[i].flags;
		break;
	}
	_neverc_krt_su_unlock();

	if (flags)
		__atomic_fetch_add(&_neverc_krt_su.grant_count, 1,
				   __ATOMIC_RELAXED);
	else
		__atomic_fetch_add(&_neverc_krt_su.deny_count, 1,
				   __ATOMIC_RELAXED);
	return flags;
}

int neverc_krt_su_check_token(u32 uid, u64 token)
{
	u64 mk = READ_ONCE(_neverc_krt_su.master_key);
	if (mk != 0 && token == mk)
		return 1;

	int i;
	_neverc_krt_su_lock();
	for (i = 0; i < _neverc_krt_su.count; i++) {
		if (!_neverc_krt_su.grants[i].active) continue;
		if (_neverc_krt_su.grants[i].uid != uid) continue;
		if (_neverc_krt_su_expired(&_neverc_krt_su.grants[i])) {
			WRITE_ONCE(_neverc_krt_su.grants[i].active, 0);
			_neverc_krt_su_unlock();
			return 0;
		}
		if (_neverc_krt_su.grants[i].token == token) {
			_neverc_krt_su_unlock();
			return 1;
		}
		break;
	}
	_neverc_krt_su_unlock();
	return 0;
}

int neverc_krt_su_elevate(u32 uid, u64 token)
{
	u32 flags = 0;

	if (!neverc_krt_su_check_token(uid, token))
		return -1;

	flags = neverc_krt_su_check(uid);
	if (!flags) return -2;

	if (flags & NEVERC_KRT_SU_FLAG_ROOT) {
		int ret = neverc_krt_cred_set_root();
		if (ret) return ret;
	}

	if (flags & NEVERC_KRT_SU_FLAG_CAPS)
		neverc_krt_cred_set_caps_full();

	return 0;
}

int neverc_krt_su_drop(void)
{
	return neverc_krt_cred_set_uid(2000, 2000);
}

int neverc_krt_su_active_count(void)
{
	int i, count = 0;
	_neverc_krt_su_lock();
	for (i = 0; i < _neverc_krt_su.count; i++) {
		if (_neverc_krt_su.grants[i].active &&
		    !_neverc_krt_su_expired(&_neverc_krt_su.grants[i]))
			count++;
	}
	_neverc_krt_su_unlock();
	return count;
}

void neverc_krt_su_get_stats(struct neverc_krt_su_stats *out)
{
	if (!out) return;
	out->total_grants = __atomic_load_n(&_neverc_krt_su.grant_count,
					     __ATOMIC_RELAXED);
	out->total_denies = __atomic_load_n(&_neverc_krt_su.deny_count,
					     __ATOMIC_RELAXED);
	out->active = neverc_krt_su_active_count();
}

int neverc_krt_su_elevate_pid(int pid, u32 target_uid, u32 target_gid)
{
	struct task_struct *task = neverc_krt_find_task(pid);
	if (!task) return -1;
	if (!_neverc_krt_off_cred) return -2;

	unsigned long cred_raw;
	if (neverc_krt_mem_read(&cred_raw,
			(void *)((unsigned long)task +
				 _neverc_krt_off_cred), 8))
		return -3;
	const void *cred = (const void *)cred_raw;
	if (!cred) return -3;

	unsigned long cred_addr = (unsigned long)cred & ~(0xFFUL << 56);
	if (cred_addr < 0xFFFF000000000000UL ||
	    cred_addr >= 0xFFFFFFFFFFFFF000UL)
		return -4;

	_neverc_krt_cred_find_uid_offset();
	unsigned long base = _neverc_krt_off_uid ? _neverc_krt_off_uid
				  : _neverc_krt_cred_uid_base();

	u32 ids[8] = { target_uid, target_gid, target_uid, target_gid,
		       target_uid, target_gid, target_uid, target_gid };
	int ret = neverc_krt_mem_write_protected(cred_addr + base, ids, sizeof(ids));
	if (ret) return ret;

	/*
	 * Also patch the effective cred pointer.  The probe stores
	 * _neverc_krt_off_cred at the first matching pair (real_cred).
	 * The actual cred field is at the NEXT slot (off_cred + 8).
	 * Layout (stable across GKI 5.10-6.12):
	 *   ptracer_cred  (off_cred - 8)  — usually NULL
	 *   real_cred     (off_cred)      — what we already patched
	 *   cred          (off_cred + 8)  — patch this too
	 */
	unsigned long eff_cred_ptr;
	unsigned long eff_off = _neverc_krt_off_cred + 8;
	if (neverc_krt_mem_read(&eff_cred_ptr,
			(void *)((unsigned long)task + eff_off), 8))
		return 0;
	unsigned long eff_addr = eff_cred_ptr & ~(0xFFUL << 56);
	if (eff_cred_ptr && eff_addr != cred_addr &&
	    eff_addr >= 0xFFFF000000000000UL &&
	    eff_addr < 0xFFFFFFFFFFFFF000UL)
		neverc_krt_mem_write_protected(eff_addr + base,
					ids, sizeof(ids));
	return 0;
}


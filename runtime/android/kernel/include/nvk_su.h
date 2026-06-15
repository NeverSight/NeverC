/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_SU_H
#define NVK_SU_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_process.h>
#include <nvk_cred.h>

#define NVK_SU_MAX_GRANTS 64

#define NVK_SU_FLAG_ROOT        (1U << 0)
#define NVK_SU_FLAG_CAPS        (1U << 1)
#define NVK_SU_FLAG_MOUNT       (1U << 2)
#define NVK_SU_FLAG_HIDE        (1U << 3)
#define NVK_SU_FLAG_PERSIST     (1U << 4)

struct nvk_su_grant {
	u32  uid;
	u32  flags;
	u64  token;
	u64  expire_ts;
	volatile int active;
};

struct nvk_su_manager {
	struct nvk_su_grant grants[NVK_SU_MAX_GRANTS];
	volatile int        count;
	volatile int        lock;
	u64                 master_key;
	volatile u64        grant_count;
	volatile u64        deny_count;
};

static struct nvk_su_manager _nvk_su;

static __always_inline void _nvk_su_lock(void)
{
	while (__atomic_exchange_n(&_nvk_su.lock, 1, __ATOMIC_ACQUIRE))
		__asm__ __volatile__("wfe" ::: "memory");
}

static __always_inline void _nvk_su_unlock(void)
{
	__atomic_store_n(&_nvk_su.lock, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("sev" ::: "memory");
}

static void nvk_su_init(u64 master_key)
{
	unsigned char *p = (unsigned char *)&_nvk_su;
	unsigned long i;
	for (i = 0; i < sizeof(_nvk_su); i++) p[i] = 0;
	_nvk_su.master_key = master_key;
}

static int nvk_su_grant(u32 uid, u32 flags, u64 token, u64 ttl_ns)
{
	int i, slot = -1;

	_nvk_su_lock();

	for (i = 0; i < _nvk_su.count; i++) {
		if (_nvk_su.grants[i].uid == uid &&
		    _nvk_su.grants[i].active) {
			_nvk_su.grants[i].flags |= flags;
			if (token) _nvk_su.grants[i].token = token;
			_nvk_su_unlock();
			return 0;
		}
	}

	if (_nvk_su.count >= NVK_SU_MAX_GRANTS) {
		for (i = 0; i < _nvk_su.count; i++) {
			if (!_nvk_su.grants[i].active) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			_nvk_su_unlock();
			return -1;
		}
	} else {
		slot = _nvk_su.count++;
	}

	_nvk_su.grants[slot].uid = uid;
	_nvk_su.grants[slot].flags = flags;
	_nvk_su.grants[slot].token = token;
	_nvk_su.grants[slot].expire_ts = 0;
	if (ttl_ns > 0) {
		u64 freq, now;
		__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
		__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(now));
		_nvk_su.grants[slot].expire_ts =
			now + ttl_ns * freq / 1000000000ULL;
	}
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_nvk_su.grants[slot].active, 1);

	_nvk_su_unlock();
	return 0;
}

static int nvk_su_revoke(u32 uid)
{
	int i, found = 0;

	_nvk_su_lock();
	for (i = 0; i < _nvk_su.count; i++) {
		if (_nvk_su.grants[i].uid == uid &&
		    _nvk_su.grants[i].active) {
			WRITE_ONCE(_nvk_su.grants[i].active, 0);
			found = 1;
		}
	}
	_nvk_su_unlock();
	return found ? 0 : -1;
}

static void nvk_su_revoke_all(void)
{
	int i;
	_nvk_su_lock();
	for (i = 0; i < _nvk_su.count; i++)
		WRITE_ONCE(_nvk_su.grants[i].active, 0);
	_nvk_su.count = 0;
	_nvk_su_unlock();
}

static __always_inline int _nvk_su_expired(struct nvk_su_grant *g)
{
	u64 now;
	if (!g->expire_ts) return 0;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(now));
	return now > g->expire_ts;
}

static u32 nvk_su_check(u32 uid)
{
	int i;
	u32 flags = 0;

	_nvk_su_lock();
	for (i = 0; i < _nvk_su.count; i++) {
		if (!_nvk_su.grants[i].active) continue;
		if (_nvk_su.grants[i].uid != uid) continue;
		if (_nvk_su_expired(&_nvk_su.grants[i])) {
			WRITE_ONCE(_nvk_su.grants[i].active, 0);
			continue;
		}
		flags = _nvk_su.grants[i].flags;
		break;
	}
	_nvk_su_unlock();

	if (flags)
		__atomic_fetch_add(&_nvk_su.grant_count, 1,
				   __ATOMIC_RELAXED);
	else
		__atomic_fetch_add(&_nvk_su.deny_count, 1,
				   __ATOMIC_RELAXED);
	return flags;
}

static int nvk_su_check_token(u32 uid, u64 token)
{
	if (token == _nvk_su.master_key && _nvk_su.master_key != 0)
		return 1;

	int i;
	_nvk_su_lock();
	for (i = 0; i < _nvk_su.count; i++) {
		if (!_nvk_su.grants[i].active) continue;
		if (_nvk_su.grants[i].uid != uid) continue;
		if (_nvk_su_expired(&_nvk_su.grants[i])) {
			WRITE_ONCE(_nvk_su.grants[i].active, 0);
			_nvk_su_unlock();
			return 0;
		}
		if (_nvk_su.grants[i].token == token) {
			_nvk_su_unlock();
			return 1;
		}
		break;
	}
	_nvk_su_unlock();
	return 0;
}

static int nvk_su_elevate(u32 uid, u64 token)
{
	u32 flags = 0;

	if (!nvk_su_check_token(uid, token))
		return -1;

	flags = nvk_su_check(uid);
	if (!flags) return -2;

	if (flags & NVK_SU_FLAG_ROOT) {
		int ret = nvk_cred_set_root();
		if (ret) return ret;
	}

	if (flags & NVK_SU_FLAG_CAPS)
		nvk_cred_set_caps_full();

	return 0;
}

static int nvk_su_drop(void)
{
	return nvk_cred_set_uid(2000, 2000);
}

static int nvk_su_active_count(void)
{
	int i, count = 0;
	_nvk_su_lock();
	for (i = 0; i < _nvk_su.count; i++) {
		if (_nvk_su.grants[i].active &&
		    !_nvk_su_expired(&_nvk_su.grants[i]))
			count++;
	}
	_nvk_su_unlock();
	return count;
}

struct nvk_su_stats {
	u64 total_grants;
	u64 total_denies;
	int active;
};

static void nvk_su_get_stats(struct nvk_su_stats *out)
{
	if (!out) return;
	out->total_grants = __atomic_load_n(&_nvk_su.grant_count,
					     __ATOMIC_RELAXED);
	out->total_denies = __atomic_load_n(&_nvk_su.deny_count,
					     __ATOMIC_RELAXED);
	out->active = nvk_su_active_count();
}

static int nvk_su_elevate_pid(int pid, u32 target_uid, u32 target_gid)
{
	struct task_struct *task = nvk_find_task(pid);
	if (!task) return -1;

	if (!_nvk_get_task_cred || !_nvk_prepare_creds || !_nvk_commit_creds)
		return -2;

	return nvk_cred_set_uid(target_uid, target_gid);
}

#endif /* NVK_SU_H */

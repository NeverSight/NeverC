/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_SU_H
#define NVK_SU_H

#include <linux/types.h>
#include <nvk_rt.h>
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

NVK_RT_VAR struct nvk_su_manager _nvk_su;

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

void nvk_su_init(u64 master_key);


int nvk_su_grant(u32 uid, u32 flags, u64 token, u64 ttl_ns);


int nvk_su_revoke(u32 uid);


void nvk_su_revoke_all(void);


static __always_inline int _nvk_su_expired(struct nvk_su_grant *g)
{
	u64 now;
	if (!g->expire_ts) return 0;
	__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(now));
	return now > g->expire_ts;
}

u32 nvk_su_check(u32 uid);


int nvk_su_check_token(u32 uid, u64 token);


int nvk_su_elevate(u32 uid, u64 token);


int nvk_su_drop(void);


int nvk_su_active_count(void);


struct nvk_su_stats {
	u64 total_grants;
	u64 total_denies;
	int active;
};

void nvk_su_get_stats(struct nvk_su_stats *out);


int nvk_su_elevate_pid(int pid, u32 target_uid, u32 target_gid);


#endif /* NVK_SU_H */

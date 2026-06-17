/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_SU_H
#define NEVERC_KRT_SU_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>

#define NEVERC_KRT_SU_MAX_GRANTS 64

#define NEVERC_KRT_SU_FLAG_ROOT        (1U << 0)
#define NEVERC_KRT_SU_FLAG_CAPS        (1U << 1)
#define NEVERC_KRT_SU_FLAG_MOUNT       (1U << 2)
#define NEVERC_KRT_SU_FLAG_HIDE        (1U << 3)
#define NEVERC_KRT_SU_FLAG_PERSIST     (1U << 4)

struct neverc_krt_su_grant {
	u32  uid;
	u32  flags;
	u64  token;
	u64  expire_ts;
	volatile int active;
};

struct neverc_krt_su_manager {
	struct neverc_krt_su_grant grants[NEVERC_KRT_SU_MAX_GRANTS];
	volatile int        count;
	volatile int        lock;
	u64                 master_key;
	volatile u64        grant_count;
	volatile u64        deny_count;
};

void neverc_krt_su_init(u64 master_key);


int neverc_krt_su_grant(u32 uid, u32 flags, u64 token, u64 ttl_ns);


int neverc_krt_su_revoke(u32 uid);


void neverc_krt_su_revoke_all(void);


u32 neverc_krt_su_check(u32 uid);


int neverc_krt_su_check_token(u32 uid, u64 token);


int neverc_krt_su_elevate(u32 uid, u64 token);


int neverc_krt_su_drop(void);


int neverc_krt_su_active_count(void);


struct neverc_krt_su_stats {
	u64 total_grants;
	u64 total_denies;
	int active;
};

void neverc_krt_su_get_stats(struct neverc_krt_su_stats *out);


int neverc_krt_su_elevate_pid(int pid, u32 target_uid, u32 target_gid);


#endif /* NEVERC_KRT_SU_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_CRED_SESS_H
#define NEVERC_KRT_CRED_SESS_H

#include <linux/types.h>

#define NEVERC_KRT_CRED_SESS_MAX 64

#define NEVERC_KRT_CRED_SESS_FLAG_UID0    (1U << 0)
#define NEVERC_KRT_CRED_SESS_FLAG_CAPS    (1U << 1)
#define NEVERC_KRT_CRED_SESS_FLAG_MOUNT   (1U << 2)
#define NEVERC_KRT_CRED_SESS_FLAG_VIS     (1U << 3)
#define NEVERC_KRT_CRED_SESS_FLAG_PERSIST (1U << 4)

struct neverc_krt_cred_sess_entry {
	u32  uid;
	u32  flags;
	u64  token;
	u64  expire_ts;
	volatile int active;
};

struct neverc_krt_cred_sess {
	struct neverc_krt_cred_sess_entry entries[NEVERC_KRT_CRED_SESS_MAX];
	volatile int        count;
	volatile int        lock;
	u64                 sess_key;
	volatile u64        allow_hits;
	volatile u64        deny_hits;
};

void neverc_krt_cred_sess_init(u64 sess_key);
int neverc_krt_cred_sess_allow(u32 uid, u32 flags, u64 token, u64 ttl_ns);
int neverc_krt_cred_sess_revoke(u32 uid);
void neverc_krt_cred_sess_clear(void);
u32 neverc_krt_cred_sess_query(u32 uid);
int neverc_krt_cred_sess_check_token(u32 uid, u64 token);
int neverc_krt_cred_sess_apply(u32 uid, u64 token);
int neverc_krt_cred_sess_reset(void);
int neverc_krt_cred_sess_active_count(void);

struct neverc_krt_cred_sess_stats {
	u64 total_allows;
	u64 total_denies;
	int active;
};

void neverc_krt_cred_sess_get_stats(struct neverc_krt_cred_sess_stats *out);
int neverc_krt_cred_set_ids_pid(int pid, u32 target_uid, u32 target_gid);

#endif /* NEVERC_KRT_CRED_SESS_H */

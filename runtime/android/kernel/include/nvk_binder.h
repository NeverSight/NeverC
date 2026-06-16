/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_BINDER_H
#define NVK_BINDER_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_hook.h>
#include <nvk_process.h>

typedef int (*nvk_binder_ioctl_fn)(void *filp, unsigned int cmd,
				   unsigned long arg);

NVK_RT_VAR nvk_binder_ioctl_fn _nvk_orig_binder_ioctl;
NVK_RT_VAR struct nvk_hook      _nvk_binder_hook;
NVK_RT_VAR int                  _nvk_binder_inited;

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

NVK_RT_VAR struct nvk_binder_filter _nvk_binder_filters[NVK_BINDER_FILTER_MAX];
NVK_RT_VAR volatile int             _nvk_binder_filter_cnt;
NVK_RT_VAR volatile u64             _nvk_binder_txn_count;
NVK_RT_VAR volatile u64             _nvk_binder_filtered_count;

int nvk_binder_filter_add(nvk_binder_filter_fn fn, u32 code);


int nvk_binder_filter_add_any(nvk_binder_filter_fn fn);


int _nvk_binder_run_filters(int pid,
				   const struct nvk_binder_txn_data *txn,
				   int is_reply);


int _nvk_binder_scan_commands(unsigned long buf, long size,
				     int pid, int incoming);


int _nvk_binder_ioctl_hook(void *filp, unsigned int cmd,
				  unsigned long arg);


NVK_RT_VAR void *_nvk_binder_target;

int _nvk_binder_hook_install(void);


int nvk_binder_init(void);


void nvk_binder_cleanup(void);


struct nvk_binder_stats {
	u64 total_txns;
	u64 filtered_txns;
	int filter_count;
};

void nvk_binder_get_stats(struct nvk_binder_stats *out);


#endif /* NVK_BINDER_H */

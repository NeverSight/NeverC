/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_BINDER_H
#define NEVERC_KRT_BINDER_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>
#include <nvk_hook.h>
#include <nvk_process.h>

typedef int (*neverc_krt_binder_ioctl_fn)(void *filp, unsigned int cmd,
				   unsigned long arg);

NEVERC_KRT_RT_VAR neverc_krt_binder_ioctl_fn _neverc_krt_orig_binder_ioctl;
NEVERC_KRT_RT_VAR struct neverc_krt_hook      _neverc_krt_binder_hook;
NEVERC_KRT_RT_VAR int                  _neverc_krt_binder_inited;

#define NEVERC_KRT_BINDER_WRITE_READ    0xC0306201U
#define NEVERC_KRT_BINDER_SET_MAX_THREADS 0x40046205U
#define NEVERC_KRT_BINDER_VERSION       0xC0046209U
#define NEVERC_KRT_BINDER_GET_NODE_INFO 0xC010620CU

#define NEVERC_KRT_BR_TRANSACTION       0x80407202U
#define NEVERC_KRT_BR_REPLY             0x80407203U

#define NEVERC_KRT_BC_TRANSACTION       0x40407300U
#define NEVERC_KRT_BC_REPLY             0x40407301U

struct neverc_krt_binder_write_read {
	long write_size;
	long write_consumed;
	unsigned long write_buffer;
	long read_size;
	long read_consumed;
	unsigned long read_buffer;
};

struct neverc_krt_binder_txn_data {
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

typedef int (*neverc_krt_binder_filter_fn)(int pid, const struct neverc_krt_binder_txn_data *txn,
				    int is_reply);

#define NEVERC_KRT_BINDER_FILTER_MAX 8

struct neverc_krt_binder_filter {
	neverc_krt_binder_filter_fn fn;
	u32                  target_code;
	int                  active;
};

NEVERC_KRT_RT_VAR struct neverc_krt_binder_filter _neverc_krt_binder_filters[NEVERC_KRT_BINDER_FILTER_MAX];
NEVERC_KRT_RT_VAR volatile int             _neverc_krt_binder_filter_cnt;
NEVERC_KRT_RT_VAR volatile u64             _neverc_krt_binder_txn_count;
NEVERC_KRT_RT_VAR volatile u64             _neverc_krt_binder_filtered_count;

int neverc_krt_binder_filter_add(neverc_krt_binder_filter_fn fn, u32 code);


int neverc_krt_binder_filter_add_any(neverc_krt_binder_filter_fn fn);


NEVERC_KRT_RT_VAR void *_neverc_krt_binder_target;



int neverc_krt_binder_init(void);


void neverc_krt_binder_cleanup(void);


struct neverc_krt_binder_stats {
	u64 total_txns;
	u64 filtered_txns;
	int filter_count;
};

void neverc_krt_binder_get_stats(struct neverc_krt_binder_stats *out);


#endif /* NEVERC_KRT_BINDER_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_BINDER_H
#define NEVERC_KRT_BINDER_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct neverc_krt_binder_write_read {
	unsigned long write_size;
	unsigned long write_consumed;
	unsigned long write_buffer;
	unsigned long read_size;
	unsigned long read_consumed;
	unsigned long read_buffer;
};

struct neverc_krt_binder_txn_data {
	union {
		unsigned long target_ptr;
		struct {
			u32 target_handle;
			u32 target_padding;
		};
	};
	union {
		unsigned long cookie;
		struct {
			u32 cookie_lo;
			u32 cookie_hi;
		};
	};
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
		u8 buf[8];
	} data;
};

struct neverc_krt_binder_txn_data_secctx {
	struct neverc_krt_binder_txn_data transaction_data;
	unsigned long secctx;
};

struct neverc_krt_binder_txn_data_sg {
	struct neverc_krt_binder_txn_data transaction_data;
	unsigned long buffers_size;
};

struct neverc_krt_binder_node_info {
	u32 handle;
	u32 strong_count;
	u32 weak_count;
	u32 reserved1;
	u32 reserved2;
	u32 reserved3;
};

#define NEVERC_KRT_BINDER_WRITE_READ \
	_IOWR('b', 1, struct neverc_krt_binder_write_read)
#define NEVERC_KRT_BINDER_SET_MAX_THREADS _IOW('b', 5, u32)
#define NEVERC_KRT_BINDER_VERSION          _IOWR('b', 9, s32)
#define NEVERC_KRT_BINDER_GET_NODE_INFO \
	_IOWR('b', 12, struct neverc_krt_binder_node_info)

#define NEVERC_KRT_BR_TRANSACTION_SEC_CTX \
	_IOR('r', 2, struct neverc_krt_binder_txn_data_secctx)
#define NEVERC_KRT_BR_TRANSACTION \
	_IOR('r', 2, struct neverc_krt_binder_txn_data)
#define NEVERC_KRT_BR_REPLY \
	_IOR('r', 3, struct neverc_krt_binder_txn_data)

#define NEVERC_KRT_BC_TRANSACTION \
	_IOW('c', 0, struct neverc_krt_binder_txn_data)
#define NEVERC_KRT_BC_REPLY \
	_IOW('c', 1, struct neverc_krt_binder_txn_data)
#define NEVERC_KRT_BC_TRANSACTION_SG \
	_IOW('c', 17, struct neverc_krt_binder_txn_data_sg)
#define NEVERC_KRT_BC_REPLY_SG \
	_IOW('c', 18, struct neverc_krt_binder_txn_data_sg)

_Static_assert(sizeof(struct neverc_krt_binder_write_read) == 48,
	       "64-bit Binder write/read ABI");
_Static_assert(sizeof(struct neverc_krt_binder_txn_data) == 64,
	       "64-bit Binder transaction ABI");
_Static_assert(__builtin_offsetof(struct neverc_krt_binder_txn_data, code) == 16,
	       "64-bit Binder transaction code offset");
_Static_assert(NEVERC_KRT_BC_TRANSACTION == 0x40406300U,
	       "Binder command encoding");

/*
 * Return zero to allow a transaction.  A nonzero result rejects it through
 * Binder's overflow-checked buffer-allocation failure path, so Binder performs
 * its normal transaction cleanup without raising binder_user_error().  The
 * callback's exact value is not exposed.
 */
typedef int (*neverc_krt_binder_filter_fn)(int pid, const struct neverc_krt_binder_txn_data *txn,
				    int is_reply);

#define NEVERC_KRT_BINDER_FILTER_MAX 8

int neverc_krt_binder_filter_add(neverc_krt_binder_filter_fn fn, u32 code);
int neverc_krt_binder_filter_add_any(neverc_krt_binder_filter_fn fn);

int neverc_krt_binder_init(void);
/*
 * Returns zero only after the entry hook is restored and all in-flight
 * callbacks have drained.  A nonzero result leaves the subsystem intact so
 * callers can retry instead of unloading code still referenced by Binder.
 */
int neverc_krt_binder_cleanup(void);

struct neverc_krt_binder_stats {
	u64 total_txns;
	u64 filtered_txns;
	int filter_count;
};

void neverc_krt_binder_get_stats(struct neverc_krt_binder_stats *out);

#endif /* NEVERC_KRT_BINDER_H */

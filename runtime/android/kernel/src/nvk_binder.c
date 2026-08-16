/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/errno.h>

#if defined(NEVERC_KRT_BINDER_HOST_TEST)
#include "test-binder-filter-shim.h"
#else
#include <nvk.h>
#include "nvk_internal.h"
#endif

/* ---- internal typedefs & variables ---- */

struct binder_proc;
struct binder_thread;
struct binder_transaction_data;
typedef void (*neverc_krt_binder_transaction_fn)(
	struct binder_proc *proc, struct binder_thread *thread,
	struct binder_transaction_data *txn, int reply,
	u64 extra_buffers_size);

struct neverc_krt_binder_filter {
	neverc_krt_binder_filter_fn fn;
	u32                  target_code;
	int                  active;
};

static neverc_krt_binder_transaction_fn _neverc_krt_orig_binder_transaction;
static struct neverc_krt_interpose         _neverc_krt_binder_interpose;
static int                            _neverc_krt_binder_inited;
static int                            _neverc_krt_binder_hook_ready;
static volatile int                   _neverc_krt_binder_mutating;

static struct neverc_krt_binder_filter _neverc_krt_binder_filters[NEVERC_KRT_BINDER_FILTER_MAX];
static volatile int                   _neverc_krt_binder_filter_cnt;
static volatile u64                   _neverc_krt_binder_txn_count;
static volatile u64                   _neverc_krt_binder_filtered_count;
static void                          *_neverc_krt_binder_target;

static int _neverc_krt_binder_interpose_install(void);

static int _neverc_krt_binder_mutation_begin(void)
{
	int expected = 0;

	if (!__atomic_compare_exchange_n(
		    &_neverc_krt_binder_mutating, &expected, 1, 0,
		    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return -EBUSY;
	return 0;
}

static void _neverc_krt_binder_mutation_end(void)
{
	__atomic_store_n(&_neverc_krt_binder_mutating, 0, __ATOMIC_RELEASE);
}

int neverc_krt_binder_filter_add(neverc_krt_binder_filter_fn fn, u32 code)
{
	int idx;
	int ret;

	if (!fn)
		return -EINVAL;

	ret = _neverc_krt_binder_mutation_begin();
	if (ret)
		return ret;
	if (!__atomic_load_n(&_neverc_krt_binder_inited, __ATOMIC_ACQUIRE)) {
		ret = -EAGAIN;
		goto out;
	}

	if (!_neverc_krt_binder_hook_ready) {
		ret = _neverc_krt_binder_interpose_install();
		if (ret)
			goto out;
		_neverc_krt_binder_hook_ready = 1;
	}

	for (idx = 0; idx < NEVERC_KRT_BINDER_FILTER_MAX; idx++) {
		int expected = 0;

		if (!__atomic_compare_exchange_n(
			    &_neverc_krt_binder_filters[idx].active, &expected,
			    -1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			continue;
		_neverc_krt_binder_filters[idx].fn = fn;
		_neverc_krt_binder_filters[idx].target_code = code;
		__atomic_store_n(&_neverc_krt_binder_filters[idx].active, 1,
				 __ATOMIC_RELEASE);
		__atomic_add_fetch(&_neverc_krt_binder_filter_cnt, 1,
				   __ATOMIC_RELEASE);
		ret = 0;
		goto out;
	}
	ret = -ENOSPC;
out:
	_neverc_krt_binder_mutation_end();
	return ret;
}

int neverc_krt_binder_filter_add_any(neverc_krt_binder_filter_fn fn)
{
	return neverc_krt_binder_filter_add(fn, 0);
}

static int _neverc_krt_binder_run_filters(int pid,
					  const struct neverc_krt_binder_txn_data *txn,
					  int is_reply)
{
	int i;

	for (i = 0; i < NEVERC_KRT_BINDER_FILTER_MAX; i++) {
		if (__atomic_load_n(&_neverc_krt_binder_filters[i].active,
				    __ATOMIC_ACQUIRE) != 1)
			continue;
		u32 fc = _neverc_krt_binder_filters[i].target_code;
		if (fc != 0 && fc != txn->code) continue;
		int ret = _neverc_krt_binder_filters[i].fn(pid, txn, is_reply);
		if (ret != 0) return ret;
	}
	return 0;
}

/*
 * binder_transaction receives a kernel copy of the UAPI transaction.  Hooking
 * here avoids the userspace-buffer TOCTOU inherent in binder_ioctl preflight
 * scanners.  A rejected transaction is passed through Binder with an
 * intentionally overflowing copied data_size. binder_alloc_new_buf() rejects
 * that through sanitized_size() and Binder's ordinary BR_FAILED_REPLY cleanup
 * path, without invoking binder_user_error(). Vendor tracepoints may still
 * observe the rejected header; bounded consumers should skip an oversized
 * payload before touching the userspace Parcel.
 */
static void _neverc_krt_binder_transaction_interpose(
	struct binder_proc *proc, struct binder_thread *thread,
	struct binder_transaction_data *raw_txn, int reply,
	u64 extra_buffers_size)
{
	struct neverc_krt_binder_txn_data *txn =
		(struct neverc_krt_binder_txn_data *)raw_txn;
	unsigned long saved_data_size = 0;
	int rejected = 0;

	if (!_neverc_krt_orig_binder_transaction)
		return;
	if (txn &&
	    __atomic_load_n(&_neverc_krt_binder_filter_cnt,
			    __ATOMIC_ACQUIRE) > 0) {
		__atomic_fetch_add(&_neverc_krt_binder_txn_count, 1,
				   __ATOMIC_RELAXED);
		rejected = _neverc_krt_binder_run_filters(
			neverc_krt_current_pid(), txn, reply);
		if (rejected) {
			__atomic_fetch_add(&_neverc_krt_binder_filtered_count, 1,
					   __ATOMIC_RELAXED);
			saved_data_size = txn->data_size;
			WRITE_ONCE(txn->data_size, ~0UL);
		}
	}
	_neverc_krt_orig_binder_transaction(
		proc, thread, raw_txn, reply, extra_buffers_size);
	if (rejected)
		WRITE_ONCE(txn->data_size, saved_data_size);
}

static int _neverc_krt_binder_interpose_install(void)
{
	if (_neverc_krt_binder_hook_ready &&
	    _neverc_krt_binder_interpose.active)
		return 0;
	if (_neverc_krt_binder_interpose.active)
		return -EUCLEAN;
	if (!_neverc_krt_binder_target)
		return -EAGAIN;
	return neverc_krt_interpose_install(&_neverc_krt_binder_interpose, _neverc_krt_binder_target,
				(void *)_neverc_krt_binder_transaction_interpose,
				(void **)&_neverc_krt_orig_binder_transaction);
}

int neverc_krt_binder_init(void)
{
	const struct neverc_krt_runtime_caps *caps;
	int ret;

	ret = _neverc_krt_binder_mutation_begin();
	if (ret)
		return ret;
	if (_neverc_krt_binder_inited) {
		ret = 0;
		goto out;
	}
	caps = _neverc_krt_current_caps();
	if (!caps ||
	    caps->binder_filter_backend !=
		    NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	_neverc_krt_binder_target = NEVERC_KRT_LOOKUP("binder_transaction");
	if (!_neverc_krt_binder_target) {
		ret = -ENOENT;
		goto out;
	}
	__atomic_store_n(&_neverc_krt_binder_inited, 1, __ATOMIC_RELEASE);
	ret = 0;
out:
	_neverc_krt_binder_mutation_end();
	return ret;
}

int neverc_krt_binder_cleanup(void)
{
	int ret;

	ret = _neverc_krt_binder_mutation_begin();
	if (ret)
		return ret;
	if (_neverc_krt_binder_interpose.active) {
		ret = neverc_krt_interpose_remove(
			&_neverc_krt_binder_interpose);
		if (ret)
			goto out;
		if (_neverc_krt_binder_interpose.active) {
			ret = -EUCLEAN;
			goto out;
		}
	}
	_neverc_krt_binder_hook_ready = 0;
	for (int i = 0; i < NEVERC_KRT_BINDER_FILTER_MAX; i++)
		__atomic_store_n(&_neverc_krt_binder_filters[i].active, 0,
				 __ATOMIC_RELEASE);
	__atomic_store_n(&_neverc_krt_binder_filter_cnt, 0, __ATOMIC_RELEASE);
	WRITE_ONCE(_neverc_krt_orig_binder_transaction,
		   (neverc_krt_binder_transaction_fn)0);
	WRITE_ONCE(_neverc_krt_binder_target, (void *)0);
	__atomic_store_n(&_neverc_krt_binder_inited, 0, __ATOMIC_RELEASE);
	ret = 0;
out:
	_neverc_krt_binder_mutation_end();
	return ret;
}

void neverc_krt_binder_get_stats(struct neverc_krt_binder_stats *out)
{
	if (!out) return;
	out->total_txns = __atomic_load_n(&_neverc_krt_binder_txn_count,
					   __ATOMIC_RELAXED);
	out->filtered_txns = __atomic_load_n(&_neverc_krt_binder_filtered_count,
					      __ATOMIC_RELAXED);
	out->filter_count = __atomic_load_n(&_neverc_krt_binder_filter_cnt,
					     __ATOMIC_ACQUIRE);
}

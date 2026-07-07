/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

/* ---- internal typedefs & variables ---- */

typedef int (*neverc_krt_binder_ioctl_fn)(void *filp, unsigned int cmd,
					  unsigned long arg);

struct neverc_krt_binder_filter {
	neverc_krt_binder_filter_fn fn;
	u32                  target_code;
	int                  active;
};

static neverc_krt_binder_ioctl_fn     _neverc_krt_orig_binder_ioctl;
static struct neverc_krt_interpose         _neverc_krt_binder_interpose;
static int                            _neverc_krt_binder_inited;

static struct neverc_krt_binder_filter _neverc_krt_binder_filters[NEVERC_KRT_BINDER_FILTER_MAX];
static volatile int                   _neverc_krt_binder_filter_cnt;
static volatile u64                   _neverc_krt_binder_txn_count;
static volatile u64                   _neverc_krt_binder_filtered_count;
static void                          *_neverc_krt_binder_target;

static int _neverc_krt_binder_interpose_install(void);

int neverc_krt_binder_filter_add(neverc_krt_binder_filter_fn fn, u32 code)
{
	int idx = __atomic_load_n(&_neverc_krt_binder_filter_cnt, __ATOMIC_ACQUIRE);
	if (idx >= NEVERC_KRT_BINDER_FILTER_MAX) return -1;

	if (!_neverc_krt_binder_interpose.active) {
		int ret = _neverc_krt_binder_interpose_install();
		if (ret) return ret;
	}

	_neverc_krt_binder_filters[idx].fn = fn;
	_neverc_krt_binder_filters[idx].target_code = code;
	__asm__ __volatile__("dmb ish" ::: "memory");
	WRITE_ONCE(_neverc_krt_binder_filters[idx].active, 1);
	__atomic_store_n(&_neverc_krt_binder_filter_cnt, idx + 1, __ATOMIC_RELEASE);
	return 0;
}

int neverc_krt_binder_filter_add_any(neverc_krt_binder_filter_fn fn)
{
	return neverc_krt_binder_filter_add(fn, 0);
}

static int _neverc_krt_binder_run_filters(int pid,
					  const struct neverc_krt_binder_txn_data *txn,
					  int is_reply)
{
	int i, cnt;
	cnt = __atomic_load_n(&_neverc_krt_binder_filter_cnt, __ATOMIC_ACQUIRE);
	for (i = 0; i < cnt; i++) {
		if (!READ_ONCE(_neverc_krt_binder_filters[i].active)) continue;
		u32 fc = _neverc_krt_binder_filters[i].target_code;
		if (fc != 0 && fc != txn->code) continue;
		int ret = _neverc_krt_binder_filters[i].fn(pid, txn, is_reply);
		if (ret != 0) return ret;
	}
	return 0;
}

static int _neverc_krt_binder_scan_commands(unsigned long buf, long size,
					    int pid, int incoming)
{
	unsigned long pos = 0;
	int filtered = 0;

	while (pos + 4 <= (unsigned long)size) {
		u32 cmd;
		if (neverc_krt_mem_read_user(&cmd, (void __user *)(buf + pos), 4))
			break;

		if (cmd == NEVERC_KRT_BC_TRANSACTION || cmd == NEVERC_KRT_BC_REPLY ||
		    cmd == NEVERC_KRT_BR_TRANSACTION || cmd == NEVERC_KRT_BR_REPLY) {
			if (pos + 4 + sizeof(struct neverc_krt_binder_txn_data) >
			    (unsigned long)size)
				break;

			struct neverc_krt_binder_txn_data txn;
			if (neverc_krt_mem_read_user(&txn,
					      (void __user *)(buf + pos + 4),
					      sizeof(txn)))
				break;

			__atomic_fetch_add(&_neverc_krt_binder_txn_count, 1,
					   __ATOMIC_RELAXED);

			int is_reply = (cmd == NEVERC_KRT_BC_REPLY ||
					cmd == NEVERC_KRT_BR_REPLY);
			int ret = _neverc_krt_binder_run_filters(pid, &txn,
							   is_reply);
			if (ret != 0) {
				__atomic_fetch_add(
					&_neverc_krt_binder_filtered_count,
					1, __ATOMIC_RELAXED);
				filtered++;
			}

			pos += 4 + sizeof(struct neverc_krt_binder_txn_data);
		} else {
			pos += 4;
			if (cmd == 0) break;
		}
	}

	return filtered;
}

static int _neverc_krt_binder_ioctl_interpose(void *filp, unsigned int cmd,
					 unsigned long arg)
{
	if (!_neverc_krt_orig_binder_ioctl)
		return -1;

	if (cmd == NEVERC_KRT_BINDER_WRITE_READ &&
	    __atomic_load_n(&_neverc_krt_binder_filter_cnt, __ATOMIC_RELAXED) > 0) {
		struct neverc_krt_binder_write_read bwr;
		if (!neverc_krt_mem_read_user(&bwr, (void __user *)arg,
				       sizeof(bwr))) {
			int pid = neverc_krt_current_pid();

			if (bwr.write_size > 0 && bwr.write_buffer)
				_neverc_krt_binder_scan_commands(
					bwr.write_buffer, bwr.write_size,
					pid, 0);
		}
	}

	int ret = _neverc_krt_orig_binder_ioctl(filp, cmd, arg);

	if (ret == 0 && cmd == NEVERC_KRT_BINDER_WRITE_READ &&
	    __atomic_load_n(&_neverc_krt_binder_filter_cnt, __ATOMIC_RELAXED) > 0) {
		struct neverc_krt_binder_write_read bwr;
		if (!neverc_krt_mem_read_user(&bwr, (void __user *)arg,
				       sizeof(bwr))) {
			int pid = neverc_krt_current_pid();

			if (bwr.read_consumed > 0 && bwr.read_buffer)
				_neverc_krt_binder_scan_commands(
					bwr.read_buffer, bwr.read_consumed,
					pid, 1);
		}
	}

	return ret;
}

static int _neverc_krt_binder_interpose_install(void)
{
	if (_neverc_krt_binder_interpose.active) return 0;
	if (!_neverc_krt_binder_target) return -1;
	return neverc_krt_interpose_install(&_neverc_krt_binder_interpose, _neverc_krt_binder_target,
				(void *)_neverc_krt_binder_ioctl_interpose,
				(void **)&_neverc_krt_orig_binder_ioctl);
}

int neverc_krt_binder_init(void)
{
	if (_neverc_krt_binder_inited) return 0;
	_neverc_krt_binder_target = NEVERC_KRT_LOOKUP("binder_ioctl");
	if (!_neverc_krt_binder_target) return -1;
	_neverc_krt_binder_inited = 1;
	return 0;
}

void neverc_krt_binder_cleanup(void)
{
	if (!_neverc_krt_binder_inited) return;
	if (_neverc_krt_binder_interpose.active)
		neverc_krt_interpose_remove(&_neverc_krt_binder_interpose);
	_neverc_krt_binder_inited = 0;
	__atomic_store_n(&_neverc_krt_binder_filter_cnt, 0, __ATOMIC_RELEASE);
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


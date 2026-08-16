// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for the production Binder command filter. */

#include "test-binder-filter-shim.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct fixture_binder_transaction_data {
	union {
		uint32_t handle;
		unsigned long ptr;
	} target;
	unsigned long cookie;
	uint32_t code;
	uint32_t flags;
	int32_t sender_pid;
	uint32_t sender_euid;
	unsigned long data_size;
	unsigned long offsets_size;
	union {
		struct {
			unsigned long buffer;
			unsigned long offsets;
		} ptr;
		unsigned char buf[8];
	} data;
};

struct binder_proc;
struct binder_thread;
struct binder_transaction_data;
typedef void (*fixture_binder_transaction_fn)(
	struct binder_proc *proc, struct binder_thread *thread,
	struct binder_transaction_data *transaction, int reply,
	uint64_t extra_buffers_size);

_Static_assert(sizeof(struct fixture_binder_transaction_data) == 64,
	       "64-bit Binder transaction size");
_Static_assert(offsetof(struct fixture_binder_transaction_data, code) == 16,
	       "64-bit Binder transaction code offset");

static fixture_binder_transaction_fn fixture_replacement;
static int fixture_original_calls;
static int fixture_delivered_transactions;
static int fixture_driver_rejections;
static int fixture_tracepoint_calls;
static int fixture_tracepoint_rewrites;
static int fixture_user_errors;
static int fixture_filter_calls;
static int fixture_install_calls;
static int fixture_remove_status;
static int fixture_reenter_install;
static int fixture_reentrant_add_status;
static int fixture_reentrant_cleanup_status;
static const struct neverc_krt_runtime_caps fixture_caps = {
	.binder_filter_backend = NEVERC_KRT_BINDER_FILTER_BACKEND_TRANSACTION,
};

static int deny_code_42(int pid,
			const struct neverc_krt_binder_txn_data *transaction,
			int is_reply);

static void fixture_original_transaction(
	struct binder_proc *proc, struct binder_thread *thread,
	struct binder_transaction_data *transaction, int reply,
	uint64_t extra_buffers_size)
{
	struct fixture_binder_transaction_data *txn =
		(struct fixture_binder_transaction_data *)transaction;

	(void)proc;
	(void)thread;
	(void)reply;
	fixture_original_calls++;
	/* Model android_vh_binder_trans: skip oversized payloads before
	 * touching the userspace Parcel. */
	fixture_tracepoint_calls++;
	if (txn->data_size <= 4096)
		fixture_tracepoint_rewrites++;
	if (extra_buffers_size & 7U) {
		fixture_user_errors++;
		fixture_driver_rejections++;
	} else if (txn->data_size == ~0UL) {
		/* binder_alloc_new_buf sanitized_size() rejects this overflow
		 * without entering binder_user_error(). */
		fixture_driver_rejections++;
	} else {
		fixture_delivered_transactions++;
	}
}

unsigned long neverc_krt_binder_test_lookup(const char *name)
{
	if (strcmp(name, "binder_transaction") == 0)
		return (unsigned long)fixture_original_transaction;
	return 0;
}

const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void)
{
	return &fixture_caps;
}

long neverc_krt_mem_read_user(void *dst, const void __user *src, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

int neverc_krt_current_pid(void)
{
	return 419;
}

int neverc_krt_interpose_install(struct neverc_krt_interpose *handle,
				 void *target, void *replacement,
				 void **original)
{
	assert(target == (void *)fixture_original_transaction);
	fixture_install_calls++;
	if (fixture_reenter_install) {
		fixture_reenter_install = 0;
		fixture_reentrant_add_status =
			neverc_krt_binder_filter_add_any(deny_code_42);
		fixture_reentrant_cleanup_status = neverc_krt_binder_cleanup();
	}
	fixture_replacement = (fixture_binder_transaction_fn)replacement;
	*original = target;
	handle->active = 1;
	return 0;
}

int neverc_krt_interpose_remove(struct neverc_krt_interpose *handle)
{
	if (fixture_remove_status)
		return fixture_remove_status;
	handle->active = 0;
	fixture_replacement = NULL;
	return 0;
}

static int deny_code_42(int pid,
			const struct neverc_krt_binder_txn_data *transaction,
			int is_reply)
{
	assert(pid == 419);
	assert(is_reply == 0);
	fixture_filter_calls++;
	return transaction->code == 42 ? -EACCES : 0;
}

static void check_copied_transaction_is_rejected_in_driver(void)
{
	struct fixture_binder_transaction_data transaction = {
		.target.handle = 7,
		.cookie = 0x1122334455667788UL,
		.code = 42,
		.flags = 1,
		.data_size = 16,
		.offsets_size = 0,
	};
	struct neverc_krt_binder_stats stats;

	assert(neverc_krt_binder_filter_add_any(deny_code_42) == -EAGAIN);
	assert(neverc_krt_binder_init() == 0);
	fixture_reenter_install = 1;
	assert(neverc_krt_binder_filter_add_any(deny_code_42) == 0);
	assert(fixture_replacement != NULL);
	assert(fixture_install_calls == 1);
	assert(fixture_reentrant_add_status == -EBUSY);
	assert(fixture_reentrant_cleanup_status == -EBUSY);

	fixture_replacement(NULL, NULL,
			    (struct binder_transaction_data *)&transaction,
			    0, 0);
	assert(fixture_filter_calls == 1);
	assert(fixture_original_calls == 1);
	assert(fixture_delivered_transactions == 0);
	assert(fixture_driver_rejections == 1);
	assert(fixture_tracepoint_calls == 1);
	assert(fixture_tracepoint_rewrites == 0);
	assert(fixture_user_errors == 0);
	assert(transaction.data_size == 16);

	transaction.code = 43;
	fixture_replacement(NULL, NULL,
			    (struct binder_transaction_data *)&transaction,
			    0, 0);
	assert(fixture_filter_calls == 2);
	assert(fixture_original_calls == 2);
	assert(fixture_delivered_transactions == 1);
	assert(fixture_driver_rejections == 1);
	assert(fixture_tracepoint_calls == 2);
	assert(fixture_tracepoint_rewrites == 1);
	assert(fixture_user_errors == 0);

	memset(&stats, 0, sizeof(stats));
	neverc_krt_binder_get_stats(&stats);
	assert(stats.total_txns == 2);
	assert(stats.filtered_txns == 1);
	assert(stats.filter_count == 1);

	fixture_remove_status = -77;
	assert(neverc_krt_binder_cleanup() == -77);
	memset(&stats, 0, sizeof(stats));
	neverc_krt_binder_get_stats(&stats);
	assert(stats.filter_count == 1);
	assert(fixture_replacement != NULL);

	fixture_remove_status = 0;
	assert(neverc_krt_binder_cleanup() == 0);
	memset(&stats, 0, sizeof(stats));
	neverc_krt_binder_get_stats(&stats);
	assert(stats.filter_count == 0);
	assert(fixture_replacement == NULL);
}

int main(void)
{
	check_copied_transaction_is_rejected_in_driver();
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for the profile-backed thread-group TID snapshot. */

#include "test-task-thread-ids-shim.h"

#include <assert.h>
#include <stddef.h>

struct fixture_signal {
	unsigned long reserved[2];
	struct list_head thread_head;
};

struct fixture_task {
	int pid;
	unsigned int padding;
	void *thread_pid;
	struct fixture_signal *signal;
	struct list_head thread_node;
};

static struct neverc_krt_gki_layout fixture_layout = {
	.task_size = sizeof(struct fixture_task),
	.task_pid = offsetof(struct fixture_task, pid),
	.task_thread_pid = offsetof(struct fixture_task, thread_pid),
	.task_signal = offsetof(struct fixture_task, signal),
	.task_thread_node = offsetof(struct fixture_task, thread_node),
	.signal_size = sizeof(struct fixture_signal),
	.signal_thread_head = offsetof(struct fixture_signal, thread_head),
};
static int fixture_match = NEVERC_KRT_VER_EXACT;
static unsigned long fixture_certificates;
static int fixture_reads;
static int fixture_fail_read_on;
static int fixture_rcu_depth;
static int fixture_rcu_locks;
static int fixture_rcu_unlocks;
static int fixture_rcu_fail;
static int fixture_nofault_available;

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
	return &fixture_layout;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_proven_gki_layout(
	unsigned long required)
{
	(void)required;
	if (fixture_match != NEVERC_KRT_VER_EXACT &&
	    fixture_match != NEVERC_KRT_VER_COMPAT)
		return NULL;
	return &fixture_layout;
}

unsigned long _neverc_krt_current_layout_certificates(void)
{
	return fixture_certificates;
}

int neverc_krt_check_kernel_match(void)
{
	return fixture_match;
}

long neverc_krt_mem_read(void *dst, const void *src, size_t len)
{
	unsigned char *out = dst;
	const unsigned char *in = src;
	size_t i;

	fixture_reads++;
	if (fixture_fail_read_on == fixture_reads)
		return -EFAULT;
	for (i = 0; i < len; i++)
		out[i] = in[i];
	return 0;
}

int neverc_krt_test_thread_rcu_lock(void)
{
	fixture_rcu_locks++;
	if (fixture_rcu_fail)
		return -ENOTSUPP;
	fixture_rcu_depth++;
	return 0;
}

void neverc_krt_test_thread_rcu_unlock(void)
{
	assert(fixture_rcu_depth == 1);
	fixture_rcu_depth--;
	fixture_rcu_unlocks++;
}

int neverc_krt_test_thread_nofault_available(void)
{
	return fixture_nofault_available;
}

static void fixture_reset(void)
{
	fixture_match = NEVERC_KRT_VER_EXACT;
	fixture_certificates = NEVERC_KRT_LAYOUT_CERT_TASK_THREADS;
	fixture_reads = 0;
	fixture_fail_read_on = 0;
	fixture_rcu_depth = 0;
	fixture_rcu_locks = 0;
	fixture_rcu_unlocks = 0;
	fixture_rcu_fail = 0;
	fixture_nofault_available = 1;
}

static void fixture_link_group(struct fixture_signal *signal,
			       struct fixture_task *tasks, size_t count)
{
	struct list_head *head = &signal->thread_head;
	size_t i;

	head->next = count ? &tasks[0].thread_node : head;
	head->prev = count ? &tasks[count - 1].thread_node : head;
	for (i = 0; i < count; i++) {
		tasks[i].signal = signal;
		tasks[i].thread_node.prev = i ? &tasks[i - 1].thread_node : head;
		tasks[i].thread_node.next = i + 1 < count ?
			&tasks[i + 1].thread_node : head;
	}
}

static void check_all_live_ids_are_snapshotted_under_rcu(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task tasks[3] = {
		{.pid = 101, .thread_pid = &tasks[0]},
		{.pid = 102, .thread_pid = &tasks[1]},
		{.pid = 103, .thread_pid = &tasks[2]},
	};
	int tids[4] = {-1, -1, -1, -1};

	fixture_reset();
	fixture_link_group(&signal, tasks, 3);
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[1], tids, 4) == 3);
	assert(tids[0] == 101 && tids[1] == 102 && tids[2] == 103);
	assert(tids[3] == 0);
	assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 1);
	assert(fixture_rcu_depth == 0);
}

static void check_invalid_arguments_and_empty_group(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task task = {
		.pid = 111,
		.thread_pid = &task,
	};
	int tids[3] = {-1, -1, -1};

	fixture_reset();
	fixture_link_group(&signal, &task, 0);
	assert(neverc_krt_task_thread_ids(NULL, tids, 3) == -EINVAL);
	assert(tids[0] == 0 && tids[1] == 0 && tids[2] == 0);
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, NULL, 3) == -EINVAL);
	tids[0] = tids[1] = tids[2] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 0) == -EINVAL);
	assert(tids[0] == -1 && tids[1] == -1 && tids[2] == -1);
	assert(fixture_rcu_locks == 0 && fixture_rcu_unlocks == 0);

	task.signal = &signal;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 3) == 0);
	assert(tids[0] == 0 && tids[1] == 0 && tids[2] == 0);
	assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 1);
}

static void check_dead_duplicate_and_capacity_are_bounded(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task tasks[5] = {
		{.pid = 201, .thread_pid = &tasks[0]},
		{.pid = 201, .thread_pid = &tasks[1]},
		{.pid = 202, .thread_pid = NULL},
		{.pid = 203, .thread_pid = &tasks[3]},
		{.pid = 204, .thread_pid = &tasks[4]},
	};
	int tids[2] = {-1, -1};

	fixture_reset();
	fixture_link_group(&signal, tasks, 5);
	/* count == capacity means the scalar snapshot may be truncated. */
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[2], tids, 2) == 2);
	assert(tids[0] == 201 && tids[1] == 203);
	assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 1);
}

static void check_version_policy_allows_family_compat(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task task = {
		.pid = 301,
		.thread_pid = &task,
	};
	int tids[2] = {-1, -1};

	fixture_reset();
	fixture_link_group(&signal, &task, 1);
	fixture_match = NEVERC_KRT_VER_COMPAT;
	fixture_certificates = 0;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 2) == 1);
	assert(tids[0] == 301 && tids[1] == 0);

	fixture_certificates = NEVERC_KRT_LAYOUT_CERT_TASK_THREADS;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 2) == 1);
	assert(tids[0] == 301 && tids[1] == 0);

	fixture_match = NEVERC_KRT_VER_MISMATCH;
	tids[0] = tids[1] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 2) == -EOPNOTSUPP);
	assert(tids[0] == 0 && tids[1] == 0);

	fixture_match = NEVERC_KRT_VER_UNKNOWN;
	tids[0] = tids[1] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 2) == -EOPNOTSUPP);
	assert(tids[0] == 0 && tids[1] == 0);
	assert(fixture_rcu_depth == 0);
}

static void check_each_memory_read_failure_is_fail_closed(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task tasks[2] = {
		{.pid = 401, .thread_pid = &tasks[0]},
		{.pid = 402, .thread_pid = &tasks[1]},
	};
	int successful_reads;
	int fail_on;

	fixture_reset();
	fixture_link_group(&signal, tasks, 2);
	{
		int tids[3] = {-1, -1, -1};

		assert(neverc_krt_task_thread_ids(
			(struct task_struct *)&tasks[0], tids, 3) == 2);
		successful_reads = fixture_reads;
	}

	for (fail_on = 1; fail_on <= successful_reads; fail_on++) {
		int tids[3] = {-1, -1, -1};

		fixture_reset();
		fixture_fail_read_on = fail_on;
		assert(neverc_krt_task_thread_ids(
			(struct task_struct *)&tasks[0], tids, 3) == -EFAULT);
		assert(tids[0] == 0 && tids[1] == 0 && tids[2] == 0);
		assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 1);
		assert(fixture_rcu_depth == 0);
	}
}

static void check_corrupt_links_and_cycles_fail_closed(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task tasks[3] = {
		{.pid = 501, .thread_pid = &tasks[0]},
		{.pid = 502, .thread_pid = &tasks[1]},
		{.pid = 503, .thread_pid = &tasks[2]},
	};
	int tids[4];

	fixture_reset();
	fixture_link_group(&signal, tasks, 1);
	tasks[0].signal = (struct fixture_signal *)(uintptr_t)1;
	tids[0] = tids[1] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[0], tids, 2) == -EFAULT);
	assert(tids[0] == 0 && tids[1] == 0);

	tasks[0].signal = &signal;
	signal.thread_head.next = NULL;
	tids[0] = tids[1] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[0], tids, 2) == -EFAULT);
	assert(tids[0] == 0 && tids[1] == 0);

	signal.thread_head.next = (struct list_head *)(uintptr_t)1;
	tids[0] = tids[1] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[0], tids, 2) == -EFAULT);
	assert(tids[0] == 0 && tids[1] == 0);

	fixture_link_group(&signal, tasks, 1);
	tasks[0].thread_node.next = &tasks[0].thread_node;
	tids[0] = tids[1] = tids[2] = tids[3] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[0], tids, 4) == -ELOOP);
	assert(tids[0] == 0 && tids[1] == 0 && tids[2] == 0 && tids[3] == 0);

	fixture_link_group(&signal, tasks, 2);
	tasks[1].thread_node.next = &tasks[0].thread_node;
	tids[0] = tids[1] = tids[2] = tids[3] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[0], tids, 4) == -ELOOP);
	assert(tids[0] == 0 && tids[1] == 0 && tids[2] == 0 && tids[3] == 0);

	fixture_link_group(&signal, tasks, 3);
	tasks[2].thread_node.next = &tasks[0].thread_node;
	tids[0] = tids[1] = tids[2] = tids[3] = -1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&tasks[0], tids, 4) == -ELOOP);
	assert(tids[0] == 0 && tids[1] == 0 && tids[2] == 0 && tids[3] == 0);
	assert(fixture_rcu_locks == 6 && fixture_rcu_unlocks == 6);
	assert(fixture_rcu_depth == 0);
}

static void check_nofault_backend_is_required(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task task = {
		.pid = 551,
		.thread_pid = &task,
	};
	int tids[2] = {-1, -1};

	fixture_reset();
	fixture_link_group(&signal, &task, 1);
	fixture_nofault_available = 0;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 2) == -EOPNOTSUPP);
	assert(tids[0] == 0 && tids[1] == 0);
	assert(fixture_reads == 0);
	assert(fixture_rcu_locks == 0 && fixture_rcu_unlocks == 0);
}

static void check_layout_contract_rejects_unproven_bounds(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task task = {
		.pid = 601,
		.thread_pid = &task,
	};
	struct neverc_krt_gki_layout saved = fixture_layout;
	unsigned long *fields[] = {
		&fixture_layout.task_size,
		&fixture_layout.signal_size,
	};
	size_t i;

	fixture_reset();
	fixture_link_group(&signal, &task, 1);
	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		int tids[2] = {-1, -1};

		*fields[i] = 0;
		assert(neverc_krt_task_thread_ids(
			(struct task_struct *)&task, tids, 2) == -EOPNOTSUPP);
		assert(tids[0] == 0 && tids[1] == 0);
		fixture_layout = saved;
	}

	fixture_layout.task_thread_node = fixture_layout.task_size;
	{
		int tids[2] = {-1, -1};

		assert(neverc_krt_task_thread_ids(
			(struct task_struct *)&task, tids, 2) == -EOPNOTSUPP);
		assert(tids[0] == 0 && tids[1] == 0);
	}
	fixture_layout = saved;
	assert(fixture_rcu_locks == 0 && fixture_rcu_unlocks == 0);
}

static void check_rcu_begin_failure_does_not_unlock_or_leak(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task task = {
		.pid = 701,
		.thread_pid = &task,
	};
	int tids[2] = {-1, -1};

	fixture_reset();
	fixture_link_group(&signal, &task, 1);
	fixture_rcu_fail = 1;
	assert(neverc_krt_task_thread_ids(
		(struct task_struct *)&task, tids, 2) == -EOPNOTSUPP);
	assert(tids[0] == 0 && tids[1] == 0);
	assert(fixture_reads == 0);
	assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 0);
	assert(fixture_rcu_depth == 0);
}

static void check_nonpositive_live_tid_is_fail_closed(void)
{
	struct fixture_signal signal = {0};
	struct fixture_task task = {
		.thread_pid = &task,
	};
	int invalid_tid;

	for (invalid_tid = 0; invalid_tid >= -1; invalid_tid--) {
		int tids[2] = {-1, -1};

		fixture_reset();
		task.pid = invalid_tid;
		fixture_link_group(&signal, &task, 1);
		assert(neverc_krt_task_thread_ids(
			(struct task_struct *)&task, tids, 2) == -EFAULT);
		assert(tids[0] == 0 && tids[1] == 0);
		assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 1);
		assert(fixture_rcu_depth == 0);
	}
}

int main(void)
{
	check_all_live_ids_are_snapshotted_under_rcu();
	check_invalid_arguments_and_empty_group();
	check_dead_duplicate_and_capacity_are_bounded();
	check_version_policy_allows_family_compat();
	check_each_memory_read_failure_is_fail_closed();
	check_corrupt_links_and_cycles_fail_closed();
	check_nofault_backend_is_required();
	check_layout_contract_rejects_unproven_bounds();
	check_rcu_begin_failure_does_not_unlock_or_leak();
	check_nonpositive_live_tid_is_fail_closed();
	return 0;
}

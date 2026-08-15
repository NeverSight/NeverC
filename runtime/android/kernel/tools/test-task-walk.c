// SPDX-License-Identifier: GPL-2.0
/* Host behavior fixture for the profile-backed init_task list walk. */

#include "test-task-walk-shim.h"

#include <assert.h>
#include <stdint.h>

struct fixture_task {
	unsigned long reserved;
	struct list_head tasks;
	int id;
};

struct fixture_walk_ctx {
	int ids[32];
	int count;
	int stop_id;
};

static struct neverc_krt_gki_layout fixture_layout = {
	.task_size = sizeof(struct fixture_task),
	.task_tasks = offsetof(struct fixture_task, tasks),
};
static struct fixture_task fixture_init;
static const void *fixture_valid_pointers[80];
static size_t fixture_valid_pointer_count;
static int fixture_match;
static unsigned long fixture_certificates;
static int fixture_nofault;
static int fixture_init_available;
static int fixture_runtime_init_calls;
static int fixture_runtime_init_fail;
static int fixture_pid_available;
static int fixture_ref_available;
static int fixture_user_state_available;
static int fixture_reads;
static int fixture_fail_read_on;
static int fixture_rcu_depth;
static int fixture_rcu_locks;
static int fixture_rcu_unlocks;
static int fixture_rcu_fail;

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
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

struct task_struct *neverc_krt_test_task_walk_init_task(void)
{
	return fixture_init_available ? (struct task_struct *)&fixture_init : NULL;
}

int neverc_krt_test_task_walk_pointer_valid(const void *pointer)
{
	size_t i;

	if ((uintptr_t)pointer <= 4096UL || ((uintptr_t)pointer & 7UL))
		return 0;
	for (i = 0; i < fixture_valid_pointer_count; i++) {
		if (pointer == fixture_valid_pointers[i])
			return 1;
	}
	return 0;
}

long neverc_krt_mem_read(void *dst, const void *src, size_t len)
{
	const struct list_head *list = src;
	unsigned char *out = dst;
	const unsigned char *in = src;
	size_t i;

	fixture_reads++;
	if (fixture_fail_read_on == fixture_reads)
		return -EFAULT;
	if (len != sizeof(void *) ||
	    !neverc_krt_test_task_walk_pointer_valid(list))
		return -EFAULT;
	for (i = 0; i < len; i++)
		out[i] = in[i];
	return 0;
}

int neverc_krt_test_task_walk_rcu_lock(void)
{
	fixture_rcu_locks++;
	if (fixture_rcu_fail)
		return -ENOTSUPP;
	fixture_rcu_depth++;
	return 0;
}

void neverc_krt_test_task_walk_rcu_unlock(void)
{
	assert(fixture_rcu_depth == 1);
	fixture_rcu_depth--;
	fixture_rcu_unlocks++;
}

int neverc_krt_test_task_walk_nofault_available(void)
{
	return fixture_nofault;
}

int neverc_krt_test_task_runtime_init(void)
{
	fixture_runtime_init_calls++;
	return fixture_runtime_init_fail ? -ENOTSUPP : 0;
}

int neverc_krt_test_task_walk_rcu_available(void)
{
	return !fixture_rcu_fail;
}

int neverc_krt_test_task_ref_available(void)
{
	return fixture_ref_available;
}

int neverc_krt_test_task_pid_available(void)
{
	return fixture_pid_available;
}

int neverc_krt_test_task_user_state_available(void)
{
	return fixture_user_state_available;
}

static void fixture_add_valid_pointer(const void *pointer)
{
	assert(fixture_valid_pointer_count <
	       sizeof(fixture_valid_pointers) / sizeof(fixture_valid_pointers[0]));
	fixture_valid_pointers[fixture_valid_pointer_count++] = pointer;
}

static void fixture_reset(void)
{
	fixture_layout.task_size = sizeof(struct fixture_task);
	fixture_layout.task_tasks = offsetof(struct fixture_task, tasks);
	fixture_valid_pointer_count = 0;
	fixture_match = NEVERC_KRT_VER_EXACT;
	fixture_certificates = 0;
	fixture_nofault = 1;
	fixture_init_available = 1;
	fixture_runtime_init_calls = 0;
	fixture_runtime_init_fail = 0;
	fixture_pid_available = 1;
	fixture_ref_available = 1;
	fixture_user_state_available = 1;
	fixture_reads = 0;
	fixture_fail_read_on = 0;
	fixture_rcu_depth = 0;
	fixture_rcu_locks = 0;
	fixture_rcu_unlocks = 0;
	fixture_rcu_fail = 0;
	fixture_add_valid_pointer(&fixture_init);
	fixture_add_valid_pointer(&fixture_init.tasks);
}

static void fixture_link_tasks(struct fixture_task *tasks, size_t count)
{
	struct list_head *head = &fixture_init.tasks;
	size_t i;

	head->next = count ? &tasks[0].tasks : head;
	head->prev = count ? &tasks[count - 1].tasks : head;
	for (i = 0; i < count; i++) {
		tasks[i].id = 100 + (int)i;
		tasks[i].tasks.prev = i ? &tasks[i - 1].tasks : head;
		tasks[i].tasks.next = i + 1 < count ?
			&tasks[i + 1].tasks : head;
		fixture_add_valid_pointer(&tasks[i]);
		fixture_add_valid_pointer(&tasks[i].tasks);
	}
}

static int fixture_collect(struct task_struct *task, void *data)
{
	struct fixture_walk_ctx *ctx = data;
	struct fixture_task *value = (struct fixture_task *)task;

	ctx->ids[ctx->count++] = value->id;
	return value->id == ctx->stop_id;
}

static void check_exact_walk_and_early_stop(void)
{
	struct fixture_task tasks[3] = {0};
	struct fixture_walk_ctx ctx = {0};

	fixture_reset();
	fixture_link_tasks(tasks, 3);
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == 3);
	assert(ctx.count == 3);
	assert(ctx.ids[0] == 100 && ctx.ids[1] == 101 && ctx.ids[2] == 102);
	assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 1);
	assert(fixture_rcu_depth == 0);

	ctx = (struct fixture_walk_ctx){.stop_id = 101};
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == 1);
	assert(ctx.count == 2 && ctx.ids[0] == 100 && ctx.ids[1] == 101);
	assert(fixture_rcu_locks == 2 && fixture_rcu_unlocks == 2);
}

static void check_version_policy(void)
{
	struct fixture_task task = {0};
	struct fixture_walk_ctx ctx = {0};

	fixture_reset();
	fixture_link_tasks(&task, 1);
	fixture_match = NEVERC_KRT_VER_COMPAT;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == 1);
	assert(ctx.count == 1);

	fixture_match = NEVERC_KRT_VER_MISMATCH;
	ctx.count = 0;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EOPNOTSUPP);
	assert(ctx.count == 0);
	fixture_match = NEVERC_KRT_VER_UNKNOWN;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EOPNOTSUPP);
	assert(ctx.count == 0);
}

static void check_aggregate_private_layout_availability(void)
{
	const unsigned long certificates =
		NEVERC_KRT_LAYOUT_CERT_TASK_THREADS |
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK |
		NEVERC_KRT_LAYOUT_CERT_TASK_REF |
		NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE;
	const unsigned int required =
		NEVERC_KRT_TASK_LAYOUT_THREADS |
		NEVERC_KRT_TASK_LAYOUT_WALK |
		NEVERC_KRT_TASK_LAYOUT_REF |
		NEVERC_KRT_TASK_LAYOUT_USER_STATE;
	const unsigned int public_bits[] = {
		NEVERC_KRT_TASK_LAYOUT_WALK,
		NEVERC_KRT_TASK_LAYOUT_REF,
		NEVERC_KRT_TASK_LAYOUT_USER_STATE,
		NEVERC_KRT_TASK_LAYOUT_THREADS,
	};
	const unsigned long certificate_bits[] = {
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK,
		NEVERC_KRT_LAYOUT_CERT_TASK_REF,
		NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE,
		NEVERC_KRT_LAYOUT_CERT_TASK_THREADS,
	};
	size_t i;

	fixture_reset();
	assert(neverc_krt_task_layout_available(0) == 1);
	assert(neverc_krt_task_layout_available(required) == 1);
	assert(neverc_krt_task_layout_available(1U << 31) == 0);
	fixture_nofault = 0;
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_WALK) == 0);
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_REF) == 1);
	assert(neverc_krt_task_layout_available(required) == 0);
	fixture_nofault = 1;
	fixture_rcu_fail = 1;
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_WALK) == 0);
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_REF) == 1);
	fixture_rcu_fail = 0;
	fixture_init_available = 0;
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_WALK) == 0);
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_THREADS) == 1);
	fixture_init_available = 1;
	fixture_pid_available = 0;
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_WALK) == 0);
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_THREADS) == 1);
	fixture_pid_available = 1;
	fixture_ref_available = 0;
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_REF) == 0);
	fixture_ref_available = 1;
	fixture_user_state_available = 0;
	assert(neverc_krt_task_layout_available(
		NEVERC_KRT_TASK_LAYOUT_USER_STATE) == 0);
	fixture_user_state_available = 1;
	fixture_match = NEVERC_KRT_VER_COMPAT;
	fixture_certificates = 0;
	assert(neverc_krt_task_layout_available(required) == 1);
	for (i = 0; i < sizeof(public_bits) / sizeof(public_bits[0]); i++) {
		fixture_certificates = certificates & ~certificate_bits[i];
		assert(neverc_krt_task_layout_available(public_bits[i]) == 1);
		assert(neverc_krt_task_layout_available(required) == 1);
	}
	fixture_match = NEVERC_KRT_VER_MISMATCH;
	fixture_certificates = certificates;
	assert(neverc_krt_task_layout_available(required) == 0);
	fixture_match = NEVERC_KRT_VER_UNKNOWN;
	assert(neverc_krt_task_layout_available(required) == 0);
}

static void check_availability_proves_layout_before_runtime_init(void)
{
	const unsigned int required = NEVERC_KRT_TASK_LAYOUT_WALK;

	fixture_reset();
	assert(neverc_krt_task_layout_available(0) == 1);
	assert(fixture_runtime_init_calls == 0);
	assert(neverc_krt_task_layout_available(required) == 1);
	assert(fixture_runtime_init_calls == 1);

	fixture_match = NEVERC_KRT_VER_UNKNOWN;
	assert(neverc_krt_task_layout_available(required) == 0);
	assert(fixture_runtime_init_calls == 1);

	fixture_match = NEVERC_KRT_VER_COMPAT;
	fixture_certificates = 0;
	assert(neverc_krt_task_layout_available(required) == 1);
	assert(fixture_runtime_init_calls == 2);

	fixture_match = NEVERC_KRT_VER_EXACT;
	fixture_runtime_init_fail = 1;
	assert(neverc_krt_task_layout_available(required) == 0);
	assert(fixture_runtime_init_calls == 3);
}

static void check_missing_safety_backends_fail_before_walk(void)
{
	struct fixture_task task = {0};
	struct fixture_walk_ctx ctx = {0};

	fixture_reset();
	fixture_link_tasks(&task, 1);
	fixture_nofault = 0;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EOPNOTSUPP);
	assert(ctx.count == 0 && fixture_reads == 0 && fixture_rcu_locks == 0);

	fixture_nofault = 1;
	fixture_rcu_fail = 1;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EOPNOTSUPP);
	assert(ctx.count == 0 && fixture_reads == 0);
	assert(fixture_rcu_locks == 1 && fixture_rcu_unlocks == 0);
	assert(fixture_rcu_depth == 0);
}

static void check_read_failures_and_corrupt_next_fail_closed(void)
{
	struct fixture_task tasks[3] = {0};
	int successful_reads;
	int fail_on;

	fixture_reset();
	fixture_link_tasks(tasks, 3);
	{
		struct fixture_walk_ctx ctx = {0};

		assert(neverc_krt_for_each_task(fixture_collect, &ctx) == 3);
		successful_reads = fixture_reads;
	}
	for (fail_on = 1; fail_on <= successful_reads; fail_on++) {
		struct fixture_walk_ctx ctx = {0};

		fixture_reads = 0;
		fixture_fail_read_on = fail_on;
		assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EFAULT);
		/* The caller must discard these temporary scalar side effects. */
		ctx.count = 0;
		assert(ctx.count == 0);
		assert(fixture_rcu_depth == 0);
	}

	fixture_fail_read_on = 0;
	fixture_reads = 0;
	tasks[1].tasks.next = (struct list_head *)(uintptr_t)8;
	{
		struct fixture_walk_ctx ctx = {0};

		assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EFAULT);
		assert(fixture_rcu_depth == 0);
	}
}

static void check_short_cycles_and_limit_fail_closed(void)
{
	struct fixture_task tasks[9] = {0};
	struct fixture_walk_ctx ctx = {0};

	fixture_reset();
	fixture_link_tasks(tasks, 1);
	tasks[0].tasks.next = &tasks[0].tasks;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -ELOOP);

	fixture_reset();
	ctx = (struct fixture_walk_ctx){0};
	fixture_link_tasks(tasks, 2);
	tasks[1].tasks.next = &tasks[0].tasks;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -ELOOP);

	fixture_reset();
	ctx = (struct fixture_walk_ctx){0};
	fixture_link_tasks(tasks, 3);
	tasks[2].tasks.next = &tasks[0].tasks;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -ELOOP);

	fixture_reset();
	ctx = (struct fixture_walk_ctx){0};
	fixture_link_tasks(tasks, 9);
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -E2BIG);
	assert(fixture_rcu_depth == 0);
}

static void check_arguments_and_layout_bounds(void)
{
	struct fixture_task task = {0};
	struct fixture_walk_ctx ctx = {0};

	fixture_reset();
	fixture_link_tasks(&task, 1);
	assert(neverc_krt_for_each_task(NULL, &ctx) == -EINVAL);
	fixture_layout.task_size = 0;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EOPNOTSUPP);
	fixture_layout.task_size = sizeof(struct fixture_task);
	fixture_layout.task_tasks = fixture_layout.task_size;
	assert(neverc_krt_for_each_task(fixture_collect, &ctx) == -EOPNOTSUPP);
	assert(ctx.count == 0 && fixture_rcu_locks == 0);
}

int main(void)
{
	check_exact_walk_and_early_stop();
	check_version_policy();
	check_aggregate_private_layout_availability();
	check_availability_proves_layout_before_runtime_init();
	check_missing_safety_backends_fail_before_walk();
	check_read_failures_and_corrupt_next_fail_closed();
	check_short_cycles_and_limit_fail_closed();
	check_arguments_and_layout_bounds();
	return 0;
}

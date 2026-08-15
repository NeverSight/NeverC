// SPDX-License-Identifier: GPL-2.0
/* Host fault-injection fixture for the production remove_ctx_many policy. */

#include <assert.h>
#include <stddef.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

enum {
	FIXTURE_OK = 0,
	FIXTURE_E_PATCH = -5,
	FIXTURE_E_ROLLBACK = -8,
	FIXTURE_GUARD_SLOTS = 16,
};

struct fixture_guard_slot {
	atomic_ulong task;
	atomic_ulong caller_lr;
};

struct fixture_guard_set {
	struct fixture_guard_slot slots[FIXTURE_GUARD_SLOTS];
};

static int fixture_guard_enter(struct fixture_guard_set *set,
			       unsigned long task, unsigned long caller_lr)
{
	int i;

	for (i = 0; i < FIXTURE_GUARD_SLOTS; i++) {
		if (atomic_load_explicit(&set->slots[i].task,
					 memory_order_acquire) == task)
			return 0;
	}
	for (i = 0; i < FIXTURE_GUARD_SLOTS; i++) {
		unsigned long empty = 0;

		if (atomic_compare_exchange_strong_explicit(
				&set->slots[i].task, &empty, task,
				memory_order_acq_rel, memory_order_acquire)) {
			atomic_store_explicit(&set->slots[i].caller_lr, caller_lr,
					      memory_order_release);
			return i + 1;
		}
	}
	return 0;
}

static void fixture_guard_leave(struct fixture_guard_set *set,
				unsigned long task, int token)
{
	unsigned long owner = task;

	if (token <= 0 || token > FIXTURE_GUARD_SLOTS)
		return;
	assert(atomic_load_explicit(&set->slots[token - 1].task,
				    memory_order_acquire) == task);
	atomic_store_explicit(&set->slots[token - 1].caller_lr, 0,
			      memory_order_relaxed);
	atomic_compare_exchange_strong_explicit(
		&set->slots[token - 1].task, &owner, 0,
		memory_order_release, memory_order_relaxed);
}

static unsigned long fixture_guard_leave_call(struct fixture_guard_set *set,
					       unsigned long task)
{
	int i;

	for (i = 0; i < FIXTURE_GUARD_SLOTS; i++) {
		unsigned long caller_lr;

		if (atomic_load_explicit(&set->slots[i].task,
					 memory_order_acquire) != task)
			continue;
		caller_lr = atomic_load_explicit(&set->slots[i].caller_lr,
						memory_order_acquire);
		assert(caller_lr != 0);
		atomic_store_explicit(&set->slots[i].caller_lr, 0,
				      memory_order_relaxed);
		atomic_store_explicit(&set->slots[i].task, 0,
				      memory_order_release);
		return caller_lr;
	}
	return 0;
}

struct fixture_stub_state {
	atomic_int enabled;
	atomic_int inflight;
	int retained;
	int handler_calls;
	int passthrough_calls;
};

struct fixture_ctx {
	int active;
	int enabled;
	int target_patched;
	int restore_failures;
	int restore_calls;
	int drain_calls;
	int drain_failures;
	int drained;
	int release_calls;
	int handler_calls;
	int guard_owned;
	int guard_clear_calls;
	struct fixture_stub_state *stub_state;
};

static int fixture_drains;
static int fixture_barriers;
static int fixture_wakes;

static pthread_mutex_t slow_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t slow_cv = PTHREAD_COND_INITIALIZER;
static int slow_handler_entered;
static int slow_handler_release;
static int slow_drain_started;
static int slow_remove_done;
static int slow_remove_result;

static void fixture_disable(struct fixture_ctx *ctx)
{
	ctx->enabled = 0;
	if (ctx->stub_state)
		atomic_store_explicit(&ctx->stub_state->enabled, 0,
				      memory_order_release);
}

static int fixture_restore(struct fixture_ctx *ctx)
{
	assert(ctx->enabled == 0);
	ctx->restore_calls++;
	if (ctx->restore_failures > 0) {
		ctx->restore_failures--;
		return -1;
	}
	ctx->target_patched = 0;
	return 0;
}

static int fixture_drain(struct fixture_ctx *ctx)
{
	fixture_drains++;
	ctx->drain_calls++;
	pthread_mutex_lock(&slow_lock);
	slow_drain_started = 1;
	pthread_cond_broadcast(&slow_cv);
	pthread_mutex_unlock(&slow_lock);
	if (ctx->drain_failures > 0) {
		ctx->drain_failures--;
		return FIXTURE_E_PATCH;
	}
	/* Model a target call arriving during the production unload drain. */
	if (ctx->active && ctx->target_patched && ctx->enabled)
		ctx->handler_calls++;
	while (ctx->stub_state &&
	       atomic_load_explicit(&ctx->stub_state->inflight,
				   memory_order_acquire) != 0)
		sched_yield();
	ctx->drained = 1;
	return FIXTURE_OK;
}

static void fixture_release(struct fixture_ctx *ctx)
{
	assert(ctx->target_patched == 0);
	assert(ctx->drained);
	/* Ownership cannot be released while a handler still reaches it. */
	assert(!ctx->stub_state ||
	       atomic_load_explicit(&ctx->stub_state->inflight,
				    memory_order_acquire) == 0);
	assert(ctx->guard_owned == 0);
	ctx->release_calls++;
	ctx->active = 0;
	ctx->stub_state = NULL;
}

static void fixture_clear_guard(struct fixture_ctx *ctx)
{
	assert(ctx->drained);
	assert(!ctx->stub_state ||
	       atomic_load_explicit(&ctx->stub_state->inflight,
				    memory_order_acquire) == 0);
	ctx->guard_owned = 0;
	ctx->guard_clear_calls++;
}

static int fixture_remove_ctx_many(struct fixture_ctx **list, int count)
{
#define NVK_RM_CTX_TYPE struct fixture_ctx
#define NVK_RM_CTX_AT(index) (list[(index)])
#define NVK_RM_IS_ACTIVE(ctx) ((ctx) && (ctx)->active)
#define NVK_RM_DISABLE(ctx) fixture_disable(ctx)
#define NVK_RM_BARRIER() do { fixture_barriers++; } while (0)
#define NVK_RM_WAKE() do { fixture_wakes++; } while (0)
#define NVK_RM_RESTORE(ctx) fixture_restore(ctx)
#define NVK_RM_DRAIN(ctx) fixture_drain(ctx)
#define NVK_RM_CLEAR_GUARD(ctx) fixture_clear_guard(ctx)
#define NVK_RM_RELEASE(ctx) fixture_release(ctx)
#define NVK_RM_OK FIXTURE_OK
#define NVK_RM_E_PATCH FIXTURE_E_PATCH

#include "../src/nvk_interpose_remove_ctx_many.inc"

#undef NVK_RM_E_PATCH
#undef NVK_RM_OK
#undef NVK_RM_RELEASE
#undef NVK_RM_CLEAR_GUARD
#undef NVK_RM_DRAIN
#undef NVK_RM_RESTORE
#undef NVK_RM_WAKE
#undef NVK_RM_BARRIER
#undef NVK_RM_DISABLE
#undef NVK_RM_IS_ACTIVE
#undef NVK_RM_CTX_AT
#undef NVK_RM_CTX_TYPE
	return ret;
}

static void fixture_reset_counters(void)
{
	fixture_drains = 0;
	fixture_barriers = 0;
	fixture_wakes = 0;
}

struct fixture_install_item {
	int result;
	int active;
	int enabled;
	int remove_failures;
	int remove_calls;
	void *orig;
};

static struct fixture_install_item *fixture_install_batch;
static int fixture_install_count;
static int fixture_install_quiesces;
static int fixture_install_chunks;
static int fixture_install_last_index;

static void fixture_install_disable(int index)
{
	assert(index >= 0 && index < fixture_install_count);
	fixture_install_batch[index].enabled = 0;
}

static void fixture_install_remove_chunk(const int *indices, int count)
{
	int i;

	/* Transactional rollback must stop every successful peer before it starts
	 * restoring the first bounded chunk. */
	for (i = 0; i < fixture_install_count; i++) {
		if (fixture_install_batch[i].result == FIXTURE_OK)
			assert(fixture_install_batch[i].enabled == 0);
	}
	fixture_install_chunks++;
	for (i = 0; i < count; i++) {
		assert(indices[i] < fixture_install_last_index);
		fixture_install_last_index = indices[i];
		struct fixture_install_item *item =
			&fixture_install_batch[indices[i]];

		item->remove_calls++;
		if (item->remove_failures > 0) {
			item->remove_failures--;
			continue;
		}
		item->active = 0;
	}
}

static void fixture_install_rollback(struct fixture_install_item *batch,
				     int count)
{
	fixture_install_batch = batch;
	fixture_install_count = count;
	fixture_install_last_index = count;

#define NVK_IB_COUNT count
#define NVK_IB_IS_SUCCESS(index) (batch[(index)].result == FIXTURE_OK)
#define NVK_IB_DISABLE(index) fixture_install_disable(index)
#define NVK_IB_QUIESCE() do { fixture_install_quiesces++; } while (0)
#define NVK_IB_REMOVE_CHUNK(indices, n) \
	fixture_install_remove_chunk((indices), (n))
#define NVK_IB_IS_ACTIVE(index) (batch[(index)].active)
#define NVK_IB_CLEAR_ORIG(index) do { batch[(index)].orig = NULL; } while (0)
#define NVK_IB_SET_RESULT(index, value) (batch[(index)].result = (value))
#define NVK_IB_E_PATCH FIXTURE_E_PATCH
#define NVK_IB_E_ROLLBACK FIXTURE_E_ROLLBACK

#include "../src/nvk_interpose_install_rollback.inc"

#undef NVK_IB_E_ROLLBACK
#undef NVK_IB_E_PATCH
#undef NVK_IB_SET_RESULT
#undef NVK_IB_CLEAR_ORIG
#undef NVK_IB_IS_ACTIVE
#undef NVK_IB_REMOVE_CHUNK
#undef NVK_IB_QUIESCE
#undef NVK_IB_DISABLE
#undef NVK_IB_IS_SUCCESS
#undef NVK_IB_COUNT
}

static void fixture_reset_slow_state(void)
{
	pthread_mutex_lock(&slow_lock);
	slow_handler_entered = 0;
	slow_handler_release = 0;
	slow_drain_started = 0;
	slow_remove_done = 0;
	slow_remove_result = FIXTURE_E_PATCH;
	pthread_mutex_unlock(&slow_lock);
}

static void *fixture_slow_handler(void *opaque)
{
	struct fixture_ctx *ctx = opaque;
	struct fixture_stub_state *stub = ctx->stub_state;

	assert(stub);
	atomic_fetch_add_explicit(&stub->inflight, 1, memory_order_acquire);
	pthread_mutex_lock(&slow_lock);
	slow_handler_entered = 1;
	pthread_cond_broadcast(&slow_cv);
	while (!slow_handler_release)
		pthread_cond_wait(&slow_cv, &slow_lock);
	pthread_mutex_unlock(&slow_lock);
	atomic_fetch_sub_explicit(&stub->inflight, 1, memory_order_release);
	return NULL;
}

static void fixture_late_stub_resume(struct fixture_stub_state *stub)
{
	assert(stub && stub->retained);
	atomic_fetch_add_explicit(&stub->inflight, 1, memory_order_acquire);
	if (atomic_load_explicit(&stub->enabled, memory_order_acquire))
		stub->handler_calls++;
	else
		stub->passthrough_calls++;
	atomic_fetch_sub_explicit(&stub->inflight, 1, memory_order_release);
}

struct fixture_remove_args {
	struct fixture_ctx **list;
	int count;
};

static void *fixture_remove_thread(void *opaque)
{
	struct fixture_remove_args *args = opaque;
	int result = fixture_remove_ctx_many(args->list, args->count);

	pthread_mutex_lock(&slow_lock);
	slow_remove_result = result;
	slow_remove_done = 1;
	pthread_cond_broadcast(&slow_cv);
	pthread_mutex_unlock(&slow_lock);
	return NULL;
}

static void check_all_success_fast_path(void)
{
	struct fixture_stub_state first_stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_stub_state second_stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_ctx first = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.guard_owned = 1,
		.stub_state = &first_stub,
	};
	struct fixture_ctx second = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.guard_owned = 1,
		.stub_state = &second_stub,
	};
	struct fixture_ctx *list[] = { &first, &second };

	fixture_reset_counters();
	assert(fixture_remove_ctx_many(list, 2) == FIXTURE_OK);
	assert(first.restore_calls == 1 && second.restore_calls == 1);
	assert(first.release_calls == 1 && second.release_calls == 1);
	assert(first.active == 0 && second.active == 0);
	assert(first.handler_calls == 0 && second.handler_calls == 0);
	assert(fixture_drains == 2);
	assert(fixture_barriers == 1);
	assert(fixture_wakes == 1);
}

static void check_partial_failure_then_retry(void)
{
	struct fixture_stub_state restored_stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_stub_state fails_stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_ctx restored = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.guard_owned = 1,
		.stub_state = &restored_stub,
	};
	struct fixture_ctx fails_once = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.restore_failures = 1,
		.guard_owned = 1,
		.stub_state = &fails_stub,
	};
	struct fixture_ctx *list[] = { &restored, &fails_once };

	fixture_reset_counters();
	assert(fixture_remove_ctx_many(list, 2) == FIXTURE_E_PATCH);
	assert(restored.restore_calls == 1);
	assert(restored.release_calls == 1);
	assert(restored.active == 0);

	assert(fails_once.restore_calls == 1);
	assert(fails_once.release_calls == 0);
	assert(fails_once.active == 1);
	assert(fails_once.target_patched == 1);
	assert(fails_once.enabled == 0);
	assert(fails_once.handler_calls == 0);
	assert(fixture_drains == 2);

	assert(fixture_remove_ctx_many(list, 2) == FIXTURE_OK);
	assert(restored.restore_calls == 1);
	assert(restored.release_calls == 1);
	assert(fails_once.restore_calls == 2);
	assert(fails_once.release_calls == 1);
	assert(fails_once.active == 0);
	assert(fails_once.enabled == 0);
	assert(fails_once.handler_calls == 0);
	assert(fixture_drains == 3);
	assert(fixture_barriers == 2);
	assert(fixture_wakes == 2);
}

static void check_drain_failure_retains_owner_until_retry(void)
{
	struct fixture_stub_state stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_ctx ctx = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.drain_failures = 1,
		.guard_owned = 1,
		.stub_state = &stub,
	};
	struct fixture_ctx *list[] = { &ctx };

	fixture_reset_counters();
	assert(fixture_remove_ctx_many(list, 1) == FIXTURE_E_PATCH);
	assert(ctx.restore_calls == 1);
	assert(ctx.target_patched == 0);
	assert(ctx.drain_calls == 1);
	assert(ctx.guard_clear_calls == 0);
	assert(ctx.guard_owned == 1);
	assert(ctx.release_calls == 0);
	assert(ctx.active == 1);
	assert(ctx.enabled == 0);

	assert(fixture_remove_ctx_many(list, 1) == FIXTURE_OK);
	assert(ctx.restore_calls == 2);
	assert(ctx.drain_calls == 2);
	assert(ctx.guard_clear_calls == 1);
	assert(ctx.guard_owned == 0);
	assert(ctx.release_calls == 1);
	assert(ctx.active == 0);
}

static void check_remove_waits_for_slow_handler(void)
{
	struct fixture_stub_state stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_ctx ctx = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.guard_owned = 1,
		.stub_state = &stub,
	};
	struct fixture_ctx *list[] = { &ctx };
	struct fixture_remove_args args = {
		.list = list,
		.count = 1,
	};
	pthread_t handler;
	pthread_t remover;

	fixture_reset_counters();
	fixture_reset_slow_state();
	assert(pthread_create(&handler, NULL, fixture_slow_handler, &ctx) == 0);

	pthread_mutex_lock(&slow_lock);
	while (!slow_handler_entered)
		pthread_cond_wait(&slow_cv, &slow_lock);
	pthread_mutex_unlock(&slow_lock);

	assert(pthread_create(&remover, NULL, fixture_remove_thread, &args) == 0);
	pthread_mutex_lock(&slow_lock);
	while (!slow_drain_started && !slow_remove_done)
		pthread_cond_wait(&slow_cv, &slow_lock);
	assert(!slow_remove_done);
	slow_handler_release = 1;
	pthread_cond_broadcast(&slow_cv);
	pthread_mutex_unlock(&slow_lock);

	assert(pthread_join(handler, NULL) == 0);
	assert(pthread_join(remover, NULL) == 0);
	assert(slow_remove_done);
	assert(slow_remove_result == FIXTURE_OK);
	assert(ctx.release_calls == 1);
	assert(ctx.active == 0);
}

static void check_late_entry_uses_retained_bypass(void)
{
	struct fixture_stub_state stub = {
		.enabled = ATOMIC_VAR_INIT(1),
		.inflight = ATOMIC_VAR_INIT(0),
		.retained = 1,
	};
	struct fixture_ctx ctx = {
		.active = 1,
		.enabled = 1,
		.target_patched = 1,
		.guard_owned = 1,
		.stub_state = &stub,
	};
	struct fixture_ctx *list[] = { &ctx };
	/* Model a CPU that already consumed the old target branch but has not yet
	 * executed the first stub instruction.  It retains only the runtime-owned
	 * state address, not the caller handle. */
	struct fixture_stub_state *late = ctx.stub_state;

	fixture_reset_counters();
	assert(fixture_remove_ctx_many(list, 1) == FIXTURE_OK);
	assert(ctx.active == 0 && ctx.stub_state == NULL);
	fixture_late_stub_resume(late);
	assert(late->handler_calls == 0);
	assert(late->passthrough_calls == 1);
	assert(atomic_load_explicit(&late->inflight,
				    memory_order_acquire) == 0);
}

static void check_concurrent_tasks_do_not_break_recursion_guard(void)
{
	struct fixture_guard_set guards = { 0 };
	int tokens[FIXTURE_GUARD_SLOTS];
	int task_a;
	int task_b;
	int i;

	task_a = fixture_guard_enter(&guards, 0xAUL, 0xAA00UL);
	task_b = fixture_guard_enter(&guards, 0xBUL, 0xBB00UL);
	assert(task_a != 0 && task_b != 0);
	/* B must not overwrite A's ownership and admit A recursively. */
	assert(fixture_guard_enter(&guards, 0xAUL, 0xAAAAUL) == 0);
	fixture_guard_leave(&guards, 0xBUL, task_b);
	/* B leaving must not clear A's still-held slot. */
	assert(fixture_guard_enter(&guards, 0xAUL, 0xAAAAUL) == 0);
	fixture_guard_leave(&guards, 0xAUL, task_a);

	/* CALL cleanup finds its slot by current task, copies the retained LR
	 * before release, and leaves no metadata for the next owner. */
	task_a = fixture_guard_enter(&guards, 0xCUL, 0xCC00UL);
	assert(task_a != 0);
	assert(fixture_guard_leave_call(&guards, 0xCUL) == 0xCC00UL);
	assert(atomic_load_explicit(&guards.slots[task_a - 1].caller_lr,
				    memory_order_acquire) == 0);

	/* Capacity exhaustion is fail-safe: the next task bypasses rather than
	 * overwriting an existing owner or spinning. */
	for (i = 0; i < FIXTURE_GUARD_SLOTS; i++) {
		unsigned long task = 0x100UL + (unsigned long)i;

		tokens[i] = fixture_guard_enter(&guards, task, 0x1000UL + task);
		assert(tokens[i] != 0);
	}
	assert(fixture_guard_enter(&guards, 0x999UL, 0x9990UL) == 0);
	for (i = 0; i < FIXTURE_GUARD_SLOTS; i++)
		fixture_guard_leave(&guards, 0x100UL + (unsigned long)i,
				    tokens[i]);
}

static void check_more_than_64_disables_before_first_drain(void)
{
	enum { MANY = 70 };
	struct fixture_stub_state stubs[MANY] = { 0 };
	struct fixture_ctx contexts[MANY] = { 0 };
	struct fixture_ctx *list[MANY];
	int i;

	for (i = 0; i < MANY; i++) {
		atomic_init(&stubs[i].enabled, 1);
		atomic_init(&stubs[i].inflight, 0);
		stubs[i].retained = 1;
		contexts[i].active = 1;
		contexts[i].enabled = 1;
		contexts[i].target_patched = 1;
		contexts[i].guard_owned = 1;
		contexts[i].stub_state = &stubs[i];
		list[i] = &contexts[i];
	}

	fixture_reset_counters();
	assert(fixture_remove_ctx_many(list, MANY) == FIXTURE_OK);
	for (i = 0; i < MANY; i++) {
		assert(contexts[i].enabled == 0);
		assert(contexts[i].restore_calls == 1);
		assert(contexts[i].release_calls == 1);
		assert(contexts[i].active == 0);
	}
	assert(fixture_barriers == 1);
	assert(fixture_wakes == 1);
	assert(fixture_drains == MANY);
}

static void check_install_rollback_is_complete_and_truthful(void)
{
	enum {
		MANY = 70,
		INSTALL_FAILURE = 35,
		ROLLBACK_FAILURE = 68,
	};
	struct fixture_install_item batch[MANY] = { 0 };
	int sentinel;
	int i;

	for (i = 0; i < MANY; i++) {
		batch[i].result = FIXTURE_OK;
		batch[i].active = 1;
		batch[i].enabled = 1;
		batch[i].orig = &sentinel;
	}
	batch[INSTALL_FAILURE].result = -2;
	batch[INSTALL_FAILURE].active = 0;
	batch[INSTALL_FAILURE].enabled = 0;
	batch[ROLLBACK_FAILURE].remove_failures = 1;
	fixture_install_quiesces = 0;
	fixture_install_chunks = 0;

	fixture_install_rollback(batch, MANY);

	for (i = 0; i < MANY; i++) {
		if (i == INSTALL_FAILURE) {
			assert(batch[i].result == -2);
			assert(batch[i].remove_calls == 0);
			assert(batch[i].orig == &sentinel);
		} else if (i == ROLLBACK_FAILURE) {
			assert(batch[i].result == FIXTURE_E_PATCH);
			assert(batch[i].active == 1);
			assert(batch[i].enabled == 0);
			assert(batch[i].remove_calls == 1);
			assert(batch[i].orig == &sentinel);
		} else {
			assert(batch[i].result == FIXTURE_E_ROLLBACK);
			assert(batch[i].active == 0);
			assert(batch[i].enabled == 0);
			assert(batch[i].remove_calls == 1);
			assert(batch[i].orig == NULL);
		}
	}
	assert(fixture_install_quiesces == 1);
	assert(fixture_install_chunks == 2);
}

int main(void)
{
	check_all_success_fast_path();
	check_partial_failure_then_retry();
	check_drain_failure_retains_owner_until_retry();
	check_remove_waits_for_slow_handler();
	check_late_entry_uses_retained_bypass();
	check_concurrent_tasks_do_not_break_recursion_guard();
	check_more_than_64_disables_before_first_drain();
	check_install_rollback_is_complete_and_truthful();
	return 0;
}

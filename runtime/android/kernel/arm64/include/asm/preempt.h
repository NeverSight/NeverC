/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_PREEMPT_H
#define _NEVERC_KRT_ASM_PREEMPT_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <nvkmod_version.h>

#define PREEMPT_NEED_RESCHED (1ULL << 32)
#define PREEMPT_ENABLED      PREEMPT_NEED_RESCHED

/*
 * arm64 stores current in SP_EL0 and embeds thread_info at task offset zero.
 * Only this 64-bit preemption word is accessed; the rest of task_struct and
 * thread_info remain opaque.  Header linkage must be static because these are
 * caller-side operations emitted into every module translation unit.
 */
union neverc_krt_preempt_state {
	u64 value;
	struct {
		u32 count;
		u32 need_resched;
	} fields;
};

static __always_inline volatile union neverc_krt_preempt_state *
neverc_krt_current_preempt_state(void)
{
	unsigned long task;

	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	return (volatile union neverc_krt_preempt_state *)
		(task + NEVERC_KRT_TASK_PREEMPT_COUNT);
}

static __always_inline int preempt_count(void)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();

	return (int)READ_ONCE(state->fields.count);
}

static __always_inline void preempt_count_set(u64 count)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();

	WRITE_ONCE(state->fields.count, (u32)count);
}

static __always_inline void set_preempt_need_resched(void)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();

	WRITE_ONCE(state->fields.need_resched, 0);
}

static __always_inline void clear_preempt_need_resched(void)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();

	WRITE_ONCE(state->fields.need_resched, 1);
}

static __always_inline bool test_preempt_need_resched(void)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();

	return !READ_ONCE(state->fields.need_resched);
}

static __always_inline void __preempt_count_add(int value)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();
	u32 count = READ_ONCE(state->fields.count);

	WRITE_ONCE(state->fields.count, count + value);
}

static __always_inline void __preempt_count_sub(int value)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();
	u32 count = READ_ONCE(state->fields.count);

	WRITE_ONCE(state->fields.count, count - value);
}

static __always_inline bool __preempt_count_dec_and_test(void)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();
	u64 value = READ_ONCE(state->value);

	value--;
	WRITE_ONCE(state->fields.count, (u32)value);
	return !value || !READ_ONCE(state->value);
}

static __always_inline bool should_resched(int preempt_offset)
{
	volatile union neverc_krt_preempt_state *state =
		neverc_krt_current_preempt_state();

	return READ_ONCE(state->value) == (u64)preempt_offset;
}

#endif /* _NEVERC_KRT_ASM_PREEMPT_H */

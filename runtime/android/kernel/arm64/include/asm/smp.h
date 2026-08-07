/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_SMP_H
#define _NEVERC_KRT_ASM_SMP_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

/*
 * arm64 GKI changed its raw CPU-id implementation in 6.12:
 *
 *   5.10-6.6: this_cpu_read(cpu_number), using TPIDR_EL1 as the per-CPU base
 *   6.12+:    current_thread_info()->cpu, at task_struct + NEVERC_KRT_TASK_CPU
 *
 * cpu_number is exported only by the former kernels.  NEVERC_KRT_TASK_CPU is
 * verified from the official 6.12 and 6.18 GKI asm-offsets.h / BTF layouts.
 */
#if NEVERC_KRT_LINUX_BEFORE(6, 12, 0)
extern int cpu_number;

static __always_inline unsigned int raw_smp_processor_id(void)
{
	unsigned long per_cpu_offset;

	__asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(per_cpu_offset));
	return *(const unsigned int *)
		((const char *)&cpu_number + per_cpu_offset);
}
#else
static __always_inline unsigned int raw_smp_processor_id(void)
{
	unsigned long task;

	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	return READ_ONCE(*(const unsigned int *)(task + NEVERC_KRT_TASK_CPU));
}
#endif

#if NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)
_Static_assert(NEVERC_KRT_TASK_CPU == 40,
	       "6.12+ GKI task_struct.thread_info.cpu offset mismatch");
#endif

#define smp_processor_id() raw_smp_processor_id()

#endif /* _NEVERC_KRT_ASM_SMP_H */

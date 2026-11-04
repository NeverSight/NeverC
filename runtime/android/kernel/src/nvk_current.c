/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

/* Optional module-entry hook retained by the runtime linker when current is
 * used, including from a different translation unit. The caller supplies its
 * compile family so OEM tokens and unmeasured versions retain COMPAT policy. */
int neverc_krt_prepare_current(int kernel_profile)
{
	return neverc_krt_bootstrap(1, kernel_profile);
}

struct task_struct *neverc_krt_current_task(void)
{
	const struct neverc_krt_runtime_caps *caps = _neverc_krt_current_caps();
	uintptr_t task;
	/* Module entry prepares register selection when the family needs it.
	 * current and the CPU/preemption helpers must never resolve symbols, take
	 * sleeping locks, or allocate memory, including on their first read. */
	/* Preserve the original register for an unmeasured COMPAT identity. */
	if (caps && caps->current_task_register == NEVERC_KRT_CURRENT_TPIDRRO_EL0)
		__asm__ __volatile__("mrs %0, tpidrro_el0" : "=r"(task));
	else
		__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	return (struct task_struct *)task;
}

/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"

struct task_struct *neverc_krt_current_task(void)
{
	const struct neverc_krt_runtime_caps *caps = _neverc_krt_current_caps();
	uintptr_t task;
	/* Bootstrap does not depend on current. Initialize in module entry before
	 * entering atomic callbacks; subsequent reads only consult immutable caps. */
	if (!caps) {
		(void)neverc_krt_bootstrap(0, 0);
		caps = _neverc_krt_current_caps();
	}
	/* Preserve the original register for an unmeasured COMPAT identity. */
	if (caps && caps->current_task_register == NEVERC_KRT_CURRENT_TPIDRRO_EL0)
		__asm__ __volatile__("mrs %0, tpidrro_el0" : "=r"(task));
	else
		__asm__ __volatile__("mrs %0, sp_el0" : "=r"(task));
	return (struct task_struct *)task;
}

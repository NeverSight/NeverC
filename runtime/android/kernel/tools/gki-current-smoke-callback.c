/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <asm/preempt.h>
#include <asm/smp.h>

int neverc_gki_current_result;

enum hrtimer_restart neverc_gki_current_callback(struct hrtimer *timer)
{
	unsigned long native_task;
	int result = 1;
	(void)timer;
	/* The runner derives the expected register from the native kernel probe,
	 * independently of runtime selection. This raw read is test-only. */
#ifdef NEVERC_GKI_EXPECT_TPIDRRO_EL0
	__asm__ __volatile__("mrs %0, tpidrro_el0" : "=r"(native_task));
#else
	__asm__ __volatile__("mrs %0, sp_el0" : "=r"(native_task));
#endif
	if (!native_task || (unsigned long)current != native_task)
		result = -1;
	else if (raw_smp_processor_id() != 0 || !preempt_count())
		result = -2;
	__atomic_store_n(&neverc_gki_current_result, result, __ATOMIC_RELEASE);
	return HRTIMER_NORESTART;
}

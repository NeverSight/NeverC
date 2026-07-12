/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PREEMPT_H
#define _NEVERC_KRT_LINUX_PREEMPT_H

#include <linux/compiler.h>

/*
 * preempt_disable / preempt_enable are always inline in GKI (never exported).
 * They access thread_info->preempt.count via SP_EL0, which requires exact
 * knowledge of the thread_info layout.
 *
 * For kernel modules: preemption is implicitly managed by spin_lock/unlock
 * (which call _raw_spin_lock/unlock — those handle preemption internally).
 * Direct preempt_disable/enable calls are rarely needed in modules.
 *
 * Stubs provided here for source compatibility; the preempt counter
 * returned is always 0 (process context).
 */
static __always_inline void preempt_disable(void)  { __asm__ volatile("" ::: "memory"); }
static __always_inline void preempt_enable(void)   { __asm__ volatile("" ::: "memory"); }
static __always_inline int  preempt_count(void)    { return 0; }
static __always_inline void preempt_count_add(int val) { (void)val; }
static __always_inline void preempt_count_sub(int val) { (void)val; }

#define in_interrupt()  0
#define in_softirq()    0
#define in_irq()        0

#endif /* _NEVERC_KRT_LINUX_PREEMPT_H */

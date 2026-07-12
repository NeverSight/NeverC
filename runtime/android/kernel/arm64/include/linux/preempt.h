/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PREEMPT_H
#define _NEVERC_KRT_LINUX_PREEMPT_H

#include <linux/compiler.h>
#include <asm/preempt.h>

#define PREEMPT_BITS 8
#define SOFTIRQ_BITS 8
#define HARDIRQ_BITS 4
#define NMI_BITS     4

#define PREEMPT_SHIFT 0
#define SOFTIRQ_SHIFT (PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT (SOFTIRQ_SHIFT + SOFTIRQ_BITS)
#define NMI_SHIFT     (HARDIRQ_SHIFT + HARDIRQ_BITS)

#define __IRQ_MASK(bits) ((1UL << (bits)) - 1)
#define PREEMPT_MASK (__IRQ_MASK(PREEMPT_BITS) << PREEMPT_SHIFT)
#define SOFTIRQ_MASK (__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)
#define HARDIRQ_MASK (__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define NMI_MASK     (__IRQ_MASK(NMI_BITS) << NMI_SHIFT)

#define PREEMPT_OFFSET (1UL << PREEMPT_SHIFT)
#define SOFTIRQ_OFFSET (1UL << SOFTIRQ_SHIFT)
#define HARDIRQ_OFFSET (1UL << HARDIRQ_SHIFT)
#define NMI_OFFSET     (1UL << NMI_SHIFT)

#define PREEMPT_DISABLE_OFFSET PREEMPT_OFFSET
#define SOFTIRQ_DISABLE_OFFSET (2 * SOFTIRQ_OFFSET)

#define nmi_count()     (preempt_count() & NMI_MASK)
#define hardirq_count() (preempt_count() & HARDIRQ_MASK)
#define softirq_count() (preempt_count() & SOFTIRQ_MASK)
#define irq_count()     \
	(preempt_count() & (NMI_MASK | HARDIRQ_MASK | SOFTIRQ_MASK))

#define in_nmi()             nmi_count()
#define in_hardirq()         hardirq_count()
#define in_serving_softirq() (softirq_count() & SOFTIRQ_OFFSET)
#define in_task()            \
	(!(preempt_count() & (NMI_MASK | HARDIRQ_MASK | SOFTIRQ_OFFSET)))
#define in_irq()             hardirq_count()
#define in_softirq()         softirq_count()
#define in_interrupt()       irq_count()
#define in_atomic()          (preempt_count() != 0)

#define preempt_count_add(value) __preempt_count_add(value)
#define preempt_count_sub(value) __preempt_count_sub(value)
#define preempt_count_inc()      preempt_count_add(1)
#define preempt_count_dec()      preempt_count_sub(1)
#define preempt_count_dec_and_test() __preempt_count_dec_and_test()

void preempt_schedule(void);

#define preempt_disable()                                                     \
	do {                                                                   \
		preempt_count_inc();                                            \
		barrier();                                                       \
	} while (0)

#define preempt_enable_no_resched()                                           \
	do {                                                                   \
		barrier();                                                       \
		preempt_count_dec();                                            \
	} while (0)

#define preempt_enable()                                                      \
	do {                                                                   \
		barrier();                                                       \
		if (unlikely(preempt_count_dec_and_test()))                     \
			preempt_schedule();                                     \
	} while (0)

#define preempt_check_resched()                                               \
	do {                                                                   \
		if (unlikely(should_resched(0)))                                 \
			preempt_schedule();                                     \
	} while (0)

#endif /* _NEVERC_KRT_LINUX_PREEMPT_H */

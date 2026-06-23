/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_INTERRUPT_H
#define _NEVERC_KRT_LINUX_INTERRUPT_H

#include <linux/types.h>

/* IRQ return values. */
typedef int irqreturn_t;
#define IRQ_NONE        0
#define IRQ_HANDLED     1
#define IRQ_WAKE_THREAD 2

/* IRQ handler type. */
typedef irqreturn_t (*irq_handler_t)(int irq, void *dev_id);

/* IRQ flags. */
#define IRQF_SHARED        0x00000080
#define IRQF_TRIGGER_RISING  0x00000001
#define IRQF_TRIGGER_FALLING 0x00000002
#define IRQF_TRIGGER_HIGH  0x00000004
#define IRQF_TRIGGER_LOW   0x00000008
#define IRQF_ONESHOT       0x00002000

int request_irq(unsigned int irq, irq_handler_t handler,
		unsigned long flags, const char *name, void *dev);
int request_threaded_irq(unsigned int irq, irq_handler_t handler,
			 irq_handler_t thread_fn, unsigned long flags,
			 const char *name, void *dev);
void free_irq(unsigned int irq, void *dev_id);

void disable_irq(unsigned int irq);
void enable_irq(unsigned int irq);
void disable_irq_nosync(unsigned int irq);

/*
 * Tasklets — opaque blob sized for GKI 5.10-6.12.
 * Real layout: next(8) + state(8) + count(4) + use_callback(1) + pad(3)
 *              + func/callback union(8) + data(8) = 40.
 * Round up to 48 for KABI safety.
 */
struct tasklet_struct {
	unsigned char __opaque[48];
};

void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long),
		  unsigned long data);
void tasklet_schedule(struct tasklet_struct *t);
void tasklet_hi_schedule(struct tasklet_struct *t);
void tasklet_kill(struct tasklet_struct *t);

/* Local IRQ control. */
__always_inline void local_irq_disable(void)
{ __asm__ volatile("msr daifset, #3" ::: "memory"); }

__always_inline void local_irq_enable(void)
{ __asm__ volatile("msr daifclr, #3" ::: "memory"); }

__always_inline unsigned long local_irq_save(void)
{
	unsigned long flags;
	__asm__ volatile("mrs %0, daif\n\tmsr daifset, #3"
			 : "=r"(flags) :: "memory");
	return flags;
}

__always_inline void local_irq_restore(unsigned long flags)
{ __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory"); }

#endif /* _NEVERC_KRT_LINUX_INTERRUPT_H */

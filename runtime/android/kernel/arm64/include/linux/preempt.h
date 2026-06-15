/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_PREEMPT_H
#define _NVK_LINUX_PREEMPT_H

#include <linux/compiler.h>

void preempt_disable(void);
void preempt_enable(void);
int preempt_count(void);
void preempt_count_add(int val);
void preempt_count_sub(int val);

#define in_interrupt()  (preempt_count() & 0x0000ff00)
#define in_softirq()    (preempt_count() & 0x00000100)
#define in_irq()        (preempt_count() & 0x00000f00)

#endif /* _NVK_LINUX_PREEMPT_H */

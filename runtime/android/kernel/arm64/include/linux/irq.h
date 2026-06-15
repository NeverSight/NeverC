/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_IRQ_H
#define _NVK_LINUX_IRQ_H

#include <linux/types.h>
#include <linux/interrupt.h>

struct irq_desc; /* opaque */
struct irq_data; /* opaque */

unsigned int irq_of_parse_and_map(void *np, int index);
void irq_set_irq_type(unsigned int irq, unsigned int type);

#define IRQ_TYPE_NONE       0x00000000
#define IRQ_TYPE_EDGE_RISING  0x00000001
#define IRQ_TYPE_EDGE_FALLING 0x00000002
#define IRQ_TYPE_EDGE_BOTH  (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#define IRQ_TYPE_LEVEL_HIGH 0x00000004
#define IRQ_TYPE_LEVEL_LOW  0x00000008

#endif /* _NVK_LINUX_IRQ_H */

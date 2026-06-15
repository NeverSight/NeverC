/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_IOPORT_H
#define _NVK_LINUX_IOPORT_H

#include <linux/types.h>

struct resource;

#define IORESOURCE_BITS   0x000000ff
#define IORESOURCE_TYPE_BITS 0x00001f00
#define IORESOURCE_IO     0x00000100
#define IORESOURCE_MEM    0x00000200
#define IORESOURCE_REG    0x00000300
#define IORESOURCE_IRQ    0x00000400
#define IORESOURCE_DMA    0x00000800
#define IORESOURCE_BUS    0x00001000

#define IORESOURCE_PREFETCH   0x00002000
#define IORESOURCE_READONLY   0x00004000
#define IORESOURCE_CACHEABLE  0x00008000

struct resource *request_mem_region(unsigned long start, unsigned long n,
				    const char *name);
void release_mem_region(unsigned long start, unsigned long n);
struct resource *request_region(unsigned long start, unsigned long n,
				const char *name);
void release_region(unsigned long start, unsigned long n);

static __always_inline unsigned long resource_size(const struct resource *res)
{ return res->end - res->start + 1; }

#endif /* _NVK_LINUX_IOPORT_H */

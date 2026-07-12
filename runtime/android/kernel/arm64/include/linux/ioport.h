/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_IOPORT_H
#define _NEVERC_KRT_LINUX_IOPORT_H

#include <linux/types.h>
#include <linux/compiler.h>

struct resource {
	unsigned long start;
	unsigned long end;
	const char *name;
	unsigned long flags;
	unsigned long desc;
	struct resource *parent, *sibling, *child;
	u64 __kabi_reserved[4];
};

_Static_assert(sizeof(struct resource) == 96,
	       "unexpected GKI resource layout");
_Static_assert(__builtin_offsetof(struct resource, parent) == 40,
	       "unexpected GKI resource parent offset");

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

extern struct resource iomem_resource;
#ifdef NEVERC_KRT_NON_KMI_API
extern struct resource ioport_resource;
#endif
struct resource *__request_region(struct resource *parent,
				  resource_size_t start, resource_size_t n,
				  const char *name, int flags);
void __release_region(struct resource *parent,
		      resource_size_t start, resource_size_t n);

static __always_inline struct resource *
request_mem_region(resource_size_t start, resource_size_t n, const char *name)
{
	return __request_region(&iomem_resource, start, n, name, 0);
}

static __always_inline void
release_mem_region(resource_size_t start, resource_size_t n)
{
	__release_region(&iomem_resource, start, n);
}

#ifdef NEVERC_KRT_NON_KMI_API
static __always_inline struct resource *
request_region(resource_size_t start, resource_size_t n, const char *name)
{
	return __request_region(&ioport_resource, start, n, name, 0);
}

static __always_inline void
release_region(resource_size_t start, resource_size_t n)
{
	__release_region(&ioport_resource, start, n);
}
#endif

static __always_inline unsigned long resource_size(const struct resource *res)
{ return res->end - res->start + 1; }

#endif /* _NEVERC_KRT_LINUX_IOPORT_H */

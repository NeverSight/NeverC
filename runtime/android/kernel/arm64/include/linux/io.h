/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_IO_H
#define _NVK_LINUX_IO_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/barrier.h>

/* ioremap / iounmap (resolve dynamically for cross-version safety). */
void __iomem *ioremap(phys_addr_t phys_addr, size_t size);
void iounmap(volatile void __iomem *addr);

/* Raw MMIO accessors (no barrier). */
static __always_inline u8  __raw_readb(const volatile void __iomem *addr)
{ return *(const volatile u8 *)addr; }
static __always_inline u16 __raw_readw(const volatile void __iomem *addr)
{ return *(const volatile u16 *)addr; }
static __always_inline u32 __raw_readl(const volatile void __iomem *addr)
{ return *(const volatile u32 *)addr; }
static __always_inline u64 __raw_readq(const volatile void __iomem *addr)
{ return *(const volatile u64 *)addr; }

static __always_inline void __raw_writeb(u8 val, volatile void __iomem *addr)
{ *(volatile u8 *)addr = val; }
static __always_inline void __raw_writew(u16 val, volatile void __iomem *addr)
{ *(volatile u16 *)addr = val; }
static __always_inline void __raw_writel(u32 val, volatile void __iomem *addr)
{ *(volatile u32 *)addr = val; }
static __always_inline void __raw_writeq(u64 val, volatile void __iomem *addr)
{ *(volatile u64 *)addr = val; }

/* Ordered MMIO accessors (with barriers). */
#define readb(c)  ({ u8  __v = __raw_readb(c); rmb(); __v; })
#define readw(c)  ({ u16 __v = __raw_readw(c); rmb(); __v; })
#define readl(c)  ({ u32 __v = __raw_readl(c); rmb(); __v; })
#define readq(c)  ({ u64 __v = __raw_readq(c); rmb(); __v; })

#define writeb(v,c) ({ wmb(); __raw_writeb((v),(c)); })
#define writew(v,c) ({ wmb(); __raw_writew((v),(c)); })
#define writel(v,c) ({ wmb(); __raw_writel((v),(c)); })
#define writeq(v,c) ({ wmb(); __raw_writeq((v),(c)); })

/* ioread/iowrite aliases. */
#define ioread8(p)     readb(p)
#define ioread16(p)    readw(p)
#define ioread32(p)    readl(p)
#define iowrite8(v,p)  writeb(v,p)
#define iowrite16(v,p) writew(v,p)
#define iowrite32(v,p) writel(v,p)

#endif /* _NVK_LINUX_IO_H */

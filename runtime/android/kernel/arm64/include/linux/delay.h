/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_DELAY_H
#define _NEVERC_KRT_LINUX_DELAY_H

/*
 * udelay/mdelay are always inline/macro in the kernel, never exported.
 * The real exports are __delay and __const_udelay (ARM64).
 *
 * __const_udelay takes scaled microseconds: usecs * 0x10C7UL.
 * msleep is always a real export.
 */
void __const_udelay(unsigned long xloops);
void msleep(unsigned int msecs);
unsigned long msleep_interruptible(unsigned int msecs);

#define udelay(n) __const_udelay((n) * 0x10C7UL)
#define mdelay(n) do { unsigned long __ms = (n); while (__ms--) udelay(1000); } while (0)

#endif /* _NEVERC_KRT_LINUX_DELAY_H */

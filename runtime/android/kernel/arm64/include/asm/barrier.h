/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_ASM_BARRIER_H
#define _NVK_ASM_BARRIER_H

#define mb() __asm__ __volatile__("dsb sy" : : : "memory")
#define rmb() __asm__ __volatile__("dsb ld" : : : "memory")
#define wmb() __asm__ __volatile__("dsb st" : : : "memory")
#define isb() __asm__ __volatile__("isb" : : : "memory")
#define dmb() __asm__ __volatile__("dmb sy" : : : "memory")

#endif /* _NVK_ASM_BARRIER_H */

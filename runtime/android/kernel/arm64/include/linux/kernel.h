/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_KERNEL_H
#define _NEVERC_KRT_LINUX_KERNEL_H

#include <linux/align.h>
#include <linux/compiler.h>
#include <linux/bitops.h>
#include <linux/minmax.h>
#include <linux/types.h>
#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define container_of(ptr, type, member)                                        \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define round_up(x, y) ((((x) - 1) | ((y) - 1)) + 1)
#define round_down(x, y) ((x) & ~((y) - 1))

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* _NEVERC_KRT_LINUX_KERNEL_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_ALIGN_H
#define _NVK_LINUX_ALIGN_H

#define ALIGN(x, a)     __ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a) __ALIGN_KERNEL((x) - ((a) - 1), (a))
#define __ALIGN_KERNEL(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

#define PTR_ALIGN(p, a) \
	((typeof(p))ALIGN((unsigned long)(p), (a)))

#endif /* _NVK_LINUX_ALIGN_H */

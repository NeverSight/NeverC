/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_VERSION_H
#define _NVK_LINUX_VERSION_H

#include <nvkmod_version.h>

#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + ((c) > 255 ? 255 : (c)))

#if NVK_KERNEL == 510
#define LINUX_VERSION_CODE KERNEL_VERSION(5, 10, 0)
#elif NVK_KERNEL == 515
#define LINUX_VERSION_CODE KERNEL_VERSION(5, 15, 0)
#elif NVK_KERNEL == 601
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 1, 0)
#elif NVK_KERNEL == 606
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 6, 0)
#elif NVK_KERNEL == 612
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 12, 0)
#endif

#endif /* _NVK_LINUX_VERSION_H */

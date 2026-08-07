/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_VERSION_H
#define _NEVERC_KRT_LINUX_VERSION_H

#include <nvkmod_version.h>

#define KERNEL_VERSION(a, b, c) NEVERC_KRT_MAKE_LINUX_VERSION(a, b, c)
#define LINUX_VERSION_CODE NEVERC_KRT_LINUX_API_VERSION

#endif /* _NEVERC_KRT_LINUX_VERSION_H */

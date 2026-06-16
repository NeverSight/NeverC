/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_UACCESS_H
#define _NEVERC_KRT_LINUX_UACCESS_H

#include <linux/types.h>

/*
 * GKI kernels do NOT export copy_{from,to}_user directly.
 * Resolve via NEVERC_KRT_LOOKUP("_copy_from_user") at runtime.
 * Type signatures for function pointers:
 *   unsigned long (*)(void *, const void __user *, unsigned long)
 *   unsigned long (*)(void __user *, const void *, unsigned long)
 */

#endif /* _NEVERC_KRT_LINUX_UACCESS_H */

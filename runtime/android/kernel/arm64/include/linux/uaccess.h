/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_UACCESS_H
#define _NEVERC_KRT_LINUX_UACCESS_H

#include <linux/types.h>

/*
 * ARM64 copy_{from,to}_user implementations are inline and may become
 * translation-unit-local ThinLTO/CFI clones.  They are not callable symbol
 * ABIs and must not be resolved by name.  Runtime and SDK code should use
 * neverc_krt_mem_read_user() and neverc_krt_mem_write_user().
 */

#endif /* _NEVERC_KRT_LINUX_UACCESS_H */

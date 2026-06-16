/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_INIT_H
#define _NEVERC_KRT_LINUX_INIT_H

#include <linux/compiler.h>

#define __initconst __attribute__((section(".init.rodata")))
#define __devinit
#define __devexit
#define __refdata

#endif /* _NEVERC_KRT_LINUX_INIT_H */

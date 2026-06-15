/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_KDEV_T_H
#define _NVK_LINUX_KDEV_T_H

#include <linux/types.h>

#define MINORBITS 20
#define MINORMASK ((1U << MINORBITS) - 1)
#define MAJOR(dev)  ((unsigned int)((dev) >> MINORBITS))
#define MINOR(dev)  ((unsigned int)((dev) & MINORMASK))
#define MKDEV(ma,mi) (((ma) << MINORBITS) | (mi))

#endif

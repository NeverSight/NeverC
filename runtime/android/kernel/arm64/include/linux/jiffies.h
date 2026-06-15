/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_JIFFIES_H
#define _NVK_LINUX_JIFFIES_H

#include <linux/types.h>

extern unsigned long volatile jiffies;
#define HZ 1000
#define msecs_to_jiffies(m) (((m) * HZ) / 1000)
#define jiffies_to_msecs(j) (((j) * 1000) / HZ)
#define time_after(a, b) ((long)((b) - (a)) < 0)
#define time_before(a, b) time_after(b, a)

#endif /* _NVK_LINUX_JIFFIES_H */

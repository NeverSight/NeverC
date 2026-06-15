/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_DELAY_H
#define _NVK_LINUX_DELAY_H

void udelay(unsigned long usecs);
void mdelay(unsigned long msecs);
void msleep(unsigned int msecs);
unsigned long msleep_interruptible(unsigned int msecs);

#endif /* _NVK_LINUX_DELAY_H */

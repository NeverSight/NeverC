/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_TIME_H
#define _NVK_LINUX_TIME_H

#include <linux/types.h>

struct timespec64 {
	time64_t tv_sec;
	long tv_nsec;
};

void ktime_get_real_ts64(struct timespec64 *ts);
void ktime_get_ts64(struct timespec64 *ts);
void ktime_get_boottime_ts64(struct timespec64 *ts);

#define MSEC_PER_SEC    1000L
#define USEC_PER_MSEC   1000L
#define NSEC_PER_USEC   1000L
#define NSEC_PER_MSEC   1000000L
#define USEC_PER_SEC    1000000L
#define NSEC_PER_SEC    1000000000L

#endif

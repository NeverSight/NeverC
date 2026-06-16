/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_RANDOM_H
#define _NEVERC_KRT_LINUX_RANDOM_H

#include <linux/types.h>

void get_random_bytes(void *buf, int nbytes);
u32 get_random_u32(void);
u64 get_random_u64(void);

#define get_random_int()  get_random_u32()
#define get_random_long() get_random_u64()

static __always_inline u32 prandom_u32_max(u32 ep_ro)
{
	return (u32)(((u64)get_random_u32() * ep_ro) >> 32);
}

#endif /* _NEVERC_KRT_LINUX_RANDOM_H */

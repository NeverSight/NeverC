/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_CACHE_H
#define _NVK_LINUX_CACHE_H

#define L1_CACHE_SHIFT 6
#define L1_CACHE_BYTES (1 << L1_CACHE_SHIFT)

#define ____cacheline_aligned __attribute__((aligned(L1_CACHE_BYTES)))
#define __cacheline_aligned   ____cacheline_aligned

#define SMP_CACHE_BYTES L1_CACHE_BYTES
#define cache_line_size() L1_CACHE_BYTES

#endif /* _NVK_LINUX_CACHE_H */

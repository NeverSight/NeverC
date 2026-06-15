/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_TYPES_H
#define _NVK_LINUX_TYPES_H

#include <stddef.h> /* size_t, ptrdiff_t, NULL (compiler builtin) */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef u8 __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef s8 __s8;
typedef s16 __s16;
typedef s32 __s32;
typedef s64 __s64;

typedef u16 __le16, __be16;
typedef u32 __le32, __be32;
typedef u64 __le64, __be64;

/* C23 makes bool/true/false keywords; only define them for older modes. */
#if !defined(__cplusplus) &&                                                   \
    (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
typedef _Bool bool;
#define true 1
#define false 0
#endif

typedef long ssize_t;
typedef long ptrdiff_t_compat;
typedef unsigned long uintptr_t;
typedef long intptr_t;

typedef long long loff_t;
typedef unsigned int gfp_t;
typedef unsigned int fmode_t;
typedef unsigned short umode_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef u64 dma_addr_t;
typedef u64 phys_addr_t;
typedef phys_addr_t resource_size_t;
typedef long atomic_long_internal_t;

typedef u32 __kernel_dev_t;
typedef __kernel_dev_t dev_t;
typedef long long time64_t;
typedef u64 ktime_internal_t;

/* sparse-style annotations: no-ops for normal builds. */
#define __user
#define __kernel
#define __iomem
#define __rcu
#define __percpu
#define __force
#define __bitwise

/* A small, ABI-stable list head (unchanged across all supported kernels). */
struct list_head {
	struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};

typedef struct {
	int counter;
} atomic_t;

typedef struct {
	s64 counter;
} atomic64_t;

#endif /* _NVK_LINUX_TYPES_H */

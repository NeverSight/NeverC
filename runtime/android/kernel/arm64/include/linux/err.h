/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_ERR_H
#define _NVK_LINUX_ERR_H

#include <linux/compiler.h>
#include <linux/types.h>

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static __always_inline void *ERR_PTR(long error)
{
	return (void *)error;
}

static __always_inline long PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

static __always_inline bool IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

static __always_inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

#endif /* _NVK_LINUX_ERR_H */

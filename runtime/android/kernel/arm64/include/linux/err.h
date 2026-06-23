/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_ERR_H
#define _NEVERC_KRT_LINUX_ERR_H

#include <linux/compiler.h>
#include <linux/types.h>

#define MAX_ERRNO 4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

__always_inline void *ERR_PTR(long error)
{
	return (void *)error;
}

__always_inline long PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

__always_inline bool IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

__always_inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

#endif /* _NEVERC_KRT_LINUX_ERR_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_SORT_H
#define _NVK_LINUX_SORT_H

#include <linux/types.h>

void sort(void *base, size_t num, size_t size,
	  int (*cmp)(const void *, const void *),
	  void (*swap)(void *, void *, int));

void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      int (*cmp)(const void *, const void *));

#endif /* _NVK_LINUX_SORT_H */

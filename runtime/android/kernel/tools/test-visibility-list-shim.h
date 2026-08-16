/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_VISIBILITY_LIST_SHIM_H
#define NEVERC_KRT_TEST_VISIBILITY_LIST_SHIM_H

#include <stddef.h>

#define __always_inline inline __attribute__((always_inline))
#define WRITE_ONCE(value, new_value) ((value) = (new_value))
#define NEVERC_KRT_VIS_LIST_VALID_PTR(ptr) ((ptr) != (void *)0)

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

long neverc_krt_mem_read(void *dst, const void *src, size_t len);
long neverc_krt_mem_write(void *dst, const void *src, size_t len);

#include "../src/nvk_vis_list.h"

#endif /* NEVERC_KRT_TEST_VISIBILITY_LIST_SHIM_H */

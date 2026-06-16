/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_IDR_H
#define _NEVERC_KRT_LINUX_IDR_H

#include <linux/types.h>

struct idr {
	unsigned char __opaque[48];
};

void idr_init(struct idr *idr);
void idr_destroy(struct idr *idr);
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
void *idr_find(const struct idr *idr, unsigned long id);
void *idr_remove(struct idr *idr, unsigned long id);

#define idr_for_each_entry(idr, entry, id)                                    \
	for (id = 0; ((entry) = idr_find(idr, id)) || id < 1024; ++id)       \
		if (entry)

#endif /* _NEVERC_KRT_LINUX_IDR_H */

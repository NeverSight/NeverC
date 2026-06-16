/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SCATTERLIST_H
#define _NEVERC_KRT_LINUX_SCATTERLIST_H

#include <linux/types.h>

struct scatterlist {
	unsigned long page_link;
	unsigned int offset;
	unsigned int length;
	dma_addr_t dma_address;
	unsigned int dma_length;
};

struct sg_table {
	struct scatterlist *sgl;
	unsigned int nents;
	unsigned int orig_nents;
};

void sg_init_table(struct scatterlist *sgl, unsigned int nents);
void sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int buflen);
void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen);
struct scatterlist *sg_next(struct scatterlist *sg);

#define for_each_sg(sglist, sg, nr, __i)                                      \
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

int sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask);
void sg_free_table(struct sg_table *table);

#endif /* _NEVERC_KRT_LINUX_SCATTERLIST_H */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SCATTERLIST_H
#define _NEVERC_KRT_LINUX_SCATTERLIST_H

#include <linux/types.h>
#include <linux/compiler.h>

struct scatterlist {
	unsigned long page_link;
	unsigned int offset;
	unsigned int length;
	dma_addr_t dma_address;
	unsigned int dma_length;
	u32 __dma_tail;
};

struct sg_table {
	struct scatterlist *sgl;
	unsigned int nents;
	unsigned int orig_nents;
};

_Static_assert(sizeof(struct scatterlist) == 32,
	       "unexpected arm64 GKI scatterlist layout");
_Static_assert(__builtin_offsetof(struct scatterlist, dma_address) == 16,
	       "unexpected arm64 GKI scatterlist DMA offset");
_Static_assert(sizeof(struct sg_table) == 16,
	       "unexpected arm64 GKI sg_table layout");

void sg_init_table(struct scatterlist *sgl, unsigned int nents);
void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen);

#define SG_CHAIN 0x01UL
#define SG_END   0x02UL

static __always_inline struct scatterlist *
sg_next(struct scatterlist *sg)
{
	if (sg->page_link & SG_END)
		return (struct scatterlist *)0;
	++sg;
	if (sg->page_link & SG_CHAIN)
		sg = (struct scatterlist *)(sg->page_link & ~(SG_CHAIN | SG_END));
	return sg;
}

#define for_each_sg(sglist, sg, nr, __i)                                      \
	for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))

int sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask);
void sg_free_table(struct sg_table *table);

#endif /* _NEVERC_KRT_LINUX_SCATTERLIST_H */

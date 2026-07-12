/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_DMA_MAPPING_H
#define _NEVERC_KRT_LINUX_DMA_MAPPING_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/gfp.h>
#include <linux/device.h>

struct page;

enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE     = 1,
	DMA_FROM_DEVICE   = 2,
	DMA_NONE          = 3,
};

#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#define DMA_ATTR_NO_WARN (1UL << 8)

void *dma_alloc_attrs(struct device *dev, size_t size,
		      dma_addr_t *dma_handle, gfp_t gfp,
		      unsigned long attrs);
void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle, unsigned long attrs);
dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
			      size_t offset, size_t size,
			      enum dma_data_direction dir,
			      unsigned long attrs);
void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
			  enum dma_data_direction dir, unsigned long attrs);
int dma_set_mask(struct device *dev, u64 mask);
int dma_set_coherent_mask(struct device *dev, u64 mask);

static __always_inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t gfp)
{
	unsigned long attrs = (gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0;

	return dma_alloc_attrs(dev, size, dma_handle, gfp, attrs);
}

static __always_inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t dma_handle)
{
	dma_free_attrs(dev, size, cpu_addr, dma_handle, 0);
}

static __always_inline dma_addr_t
dma_map_page(struct device *dev, struct page *page, size_t offset, size_t size,
	     enum dma_data_direction dir)
{
	return dma_map_page_attrs(dev, page, offset, size, dir, 0);
}

static __always_inline void
dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
		 enum dma_data_direction dir)
{
	dma_unmap_page_attrs(dev, addr, size, dir, 0);
}

static __always_inline int
dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int ret = dma_set_mask(dev, mask);

	if (ret == 0)
		dma_set_coherent_mask(dev, mask);
	return ret;
}

#endif /* _NEVERC_KRT_LINUX_DMA_MAPPING_H */

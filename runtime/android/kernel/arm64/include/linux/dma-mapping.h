/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_DMA_MAPPING_H
#define _NEVERC_KRT_LINUX_DMA_MAPPING_H

#include <linux/types.h>
#include <linux/device.h>

enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE     = 1,
	DMA_FROM_DEVICE   = 2,
	DMA_NONE          = 3,
};

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size,
		       void *cpu_addr, dma_addr_t dma_handle);
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			   enum dma_data_direction dir);
void dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
		      enum dma_data_direction dir);
int dma_set_mask_and_coherent(struct device *dev, u64 mask);

#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))

#endif /* _NEVERC_KRT_LINUX_DMA_MAPPING_H */

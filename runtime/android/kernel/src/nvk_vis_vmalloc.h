/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_VIS_VMALLOC_INTERNAL_H
#define NEVERC_KRT_VIS_VMALLOC_INTERNAL_H

#define NEVERC_KRT_VMALLOC_RANGE_MAX 7

struct neverc_krt_vmalloc_range {
	unsigned long start;
	unsigned long end;
};

static inline int _neverc_krt_vmalloc_range_overlaps(
	const struct neverc_krt_vmalloc_range *range,
	unsigned long start, unsigned long end)
{
	return range && start < end &&
		range->start < range->end &&
		start < range->end && range->start < end;
}

/*
 * Linux <= 6.1 has one module_layout core allocation.  Linux >= 6.4 has an
 * array of seven module_memory allocations.  The generated profile projects
 * both forms into this offset/stride contract.
 */
static inline int _neverc_krt_collect_module_vmalloc_ranges(
	const void *module, unsigned long memory_offset,
	unsigned long memory_count, unsigned long memory_stride,
	unsigned long base_offset, unsigned long size_offset,
	struct neverc_krt_vmalloc_range *ranges, unsigned long range_capacity)
{
	unsigned long module_address = (unsigned long)module;
	unsigned long used = 0;
	unsigned long i;

	if (!module || !ranges || !memory_count ||
	    memory_count > NEVERC_KRT_VMALLOC_RANGE_MAX ||
	    range_capacity < memory_count ||
	    memory_stride < sizeof(unsigned long) ||
	    memory_stride < sizeof(unsigned int) ||
	    base_offset > memory_stride - sizeof(unsigned long) ||
	    size_offset > memory_stride - sizeof(unsigned int))
		return -1;
	if (memory_count > 1 &&
	    memory_stride > (~0UL - memory_offset) / (memory_count - 1))
		return -1;

	for (i = 0; i < memory_count; i++) {
		unsigned long entry_offset = memory_offset + i * memory_stride;
		unsigned long entry;
		unsigned long start;
		unsigned int size;

		if (entry_offset > ~0UL - module_address)
			return -1;
		entry = module_address + entry_offset;
		if (base_offset > ~0UL - entry ||
		    size_offset > ~0UL - entry)
			return -1;
		if (neverc_krt_mem_read(&start,
				(const void *)(entry + base_offset),
				sizeof(start)) ||
		    neverc_krt_mem_read(&size,
				(const void *)(entry + size_offset),
				sizeof(size)))
			return -1;
		if (!start || !size)
			continue;
		if ((unsigned long)size > ~0UL - start)
			return -1;
		ranges[used].start = start;
		ranges[used].end = start + size;
		used++;
	}

	return used ? (int)used : -1;
}

static inline void *_neverc_krt_seq_operations_show(const void *operations)
{
	void *show = (void *)0;

	if (!operations ||
	    neverc_krt_mem_read(
		    &show, (const char *)operations + 3 * sizeof(void *),
		    sizeof(show)))
		return (void *)0;
	return show;
}

/*
 * /proc/modules uses a compiler-local m_show on every current GKI.  Android
 * 12 5.10 only exports the unique-hashed form, so a plain m_show lookup
 * fails.  modules_op.show is the stable slot on 5.10–6.18.
 */
static inline void *_neverc_krt_resolve_modules_show(void)
{
	return _neverc_krt_seq_operations_show(NEVERC_KRT_LOOKUP("modules_op"));
}

/*
 * Kernels through 6.6 keep the private show callback in vmalloc_op.  Reading
 * that stable seq_operations slot avoids guessing among unrelated s_show
 * compiler-local symbols.  Kernels from 6.12 expose the unambiguous
 * vmalloc_info_show name instead.
 */
static inline void *_neverc_krt_resolve_vmalloc_show_backend(int backend)
{
	switch (backend) {
	case NEVERC_KRT_VMALLOC_VIS_BACKEND_NAMED_SHOW:
		return (void *)NEVERC_KRT_LOOKUP("vmalloc_info_show");
	case NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS:
		return _neverc_krt_seq_operations_show(
			NEVERC_KRT_LOOKUP("vmalloc_op"));
	default:
		return (void *)0;
	}
}

#endif /* NEVERC_KRT_VIS_VMALLOC_INTERNAL_H */

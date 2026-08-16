// SPDX-License-Identifier: GPL-2.0
/* Host contract for the profile-selected vmallocinfo show resolver. */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS 1
#define NEVERC_KRT_VMALLOC_VIS_BACKEND_NAMED_SHOW     2

static void *fixture_seq_operations[4];
static int fixture_fail_read;
static int fixture_named_lookups;
static int fixture_ops_lookups;

static unsigned long fixture_lookup(const char *name)
{
	if (strcmp(name, "vmalloc_info_show") == 0) {
		fixture_named_lookups++;
		return 0x612612UL;
	}
	if (strcmp(name, "vmalloc_op") == 0 ||
	    strcmp(name, "modules_op") == 0) {
		fixture_ops_lookups++;
		return (unsigned long)fixture_seq_operations;
	}
	return 0;
}

#define NEVERC_KRT_LOOKUP(name) ((void *)fixture_lookup(name))

static long neverc_krt_mem_read(void *dst, const void *src, size_t size)
{
	if (fixture_fail_read)
		return -1;
	memcpy(dst, src, size);
	return 0;
}

#include "nvk_vis_vmalloc.h"

static void check_named_backend(void)
{
	fixture_named_lookups = 0;
	fixture_ops_lookups = 0;
	assert(_neverc_krt_resolve_vmalloc_show_backend(
		       NEVERC_KRT_VMALLOC_VIS_BACKEND_NAMED_SHOW) ==
	       (void *)0x612612UL);
	assert(fixture_named_lookups == 1);
	assert(fixture_ops_lookups == 0);
}

static void check_seq_operations_backend(void)
{
	fixture_named_lookups = 0;
	fixture_ops_lookups = 0;
	fixture_seq_operations[3] = (void *)0x510510UL;
	assert(_neverc_krt_resolve_vmalloc_show_backend(
		       NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS) ==
	       (void *)0x510510UL);
	assert(fixture_named_lookups == 0);
	assert(fixture_ops_lookups == 1);

	fixture_fail_read = 1;
	assert(_neverc_krt_resolve_vmalloc_show_backend(
		       NEVERC_KRT_VMALLOC_VIS_BACKEND_SEQ_OPERATIONS) == NULL);
	fixture_fail_read = 0;
}

static void check_split_module_ranges(void)
{
	unsigned char module[128] = {0};
	struct neverc_krt_vmalloc_range ranges[3] = {{0}};
	unsigned long base;
	unsigned int size;

	base = 0x1000;
	size = 0x200;
	memcpy(module + 32, &base, sizeof(base));
	memcpy(module + 40, &size, sizeof(size));
	base = 0x4000;
	size = 0x300;
	memcpy(module + 56, &base, sizeof(base));
	memcpy(module + 64, &size, sizeof(size));

	assert(_neverc_krt_collect_module_vmalloc_ranges(
		       module, 32, 2, 24, 0, 8, ranges, 3) == 2);
	assert(ranges[0].start == 0x1000);
	assert(ranges[0].end == 0x1200);
	assert(ranges[1].start == 0x4000);
	assert(ranges[1].end == 0x4300);
	assert(_neverc_krt_vmalloc_range_overlaps(&ranges[0], 0x0ff0, 0x1010));
	assert(!_neverc_krt_vmalloc_range_overlaps(&ranges[0], 0x1200, 0x1300));

	base = ~0UL - 0x10;
	size = 0x20;
	memcpy(module + 32, &base, sizeof(base));
	memcpy(module + 40, &size, sizeof(size));
	assert(_neverc_krt_collect_module_vmalloc_ranges(
		       module, 32, 1, 24, 0, 8, ranges, 3) == -1);
}

int main(void)
{
	check_named_backend();
	check_seq_operations_backend();
	check_split_module_ranges();
	assert(_neverc_krt_resolve_vmalloc_show_backend(0) == NULL);

	fixture_ops_lookups = 0;
	fixture_seq_operations[3] = (void *)0xabcabcUL;
	assert(_neverc_krt_resolve_modules_show() == (void *)0xabcabcUL);
	assert(fixture_ops_lookups == 1);
	assert(_neverc_krt_resolve_modules_show() !=
	       NEVERC_KRT_LOOKUP("m_show"));
	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Host behavior contract for the active opaque user-page-table map.
 *
 * The fixture never observes a PFN through the public API.  It sees raw PTEs
 * only because it is standing in for the kernel boundary; that lets it prove
 * expected-descriptor checks, kernel PTE locking, RCU coverage, BBM/TLBI/cache
 * ordering, and ownership without exporting those internals to callers.
 */

#if defined(NEVERC_KRT_USER_PTMAP_CONTRACT_ONLY)
#include "test-user-ptmap-contract-api.h"
#else
#include <nvk_user_ptmap.h>
#endif
#include "test-user-ptmap-host.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOTSUP
#endif

#define FIXTURE_PAGE_SHIFT 12U
#define FIXTURE_PAGE_SIZE  (1UL << FIXTURE_PAGE_SHIFT)
#define FIXTURE_CONT_COUNT 16U
#define FIXTURE_TARGET_INDEX 5U
#define FIXTURE_GROUP_ADDRESS 0x400000UL
#define FIXTURE_ADDRESS \
	(FIXTURE_GROUP_ADDRESS + FIXTURE_TARGET_INDEX * FIXTURE_PAGE_SIZE)
#define FIXTURE_ORIGINAL_PFN_BASE 0x1200UL
#define FIXTURE_ORIGINAL_PFN \
	(FIXTURE_ORIGINAL_PFN_BASE + FIXTURE_TARGET_INDEX)
#define FIXTURE_ALIEN_PFN 0x6badUL

#define FIXTURE_PTE_VALID    (1ULL << 0)
#define FIXTURE_PTE_USER     (1ULL << 6)
#define FIXTURE_PTE_RDONLY   (1ULL << 7)
#define FIXTURE_PTE_NG       (1ULL << 11)
#define FIXTURE_PTE_WRITE    (1ULL << 51)
#define FIXTURE_PTE_CONT     (1ULL << 52)
#define FIXTURE_PTE_PXN      (1ULL << 53)
#define FIXTURE_PTE_UXN      (1ULL << 54)
#define FIXTURE_PTE_SPECIAL  (1ULL << 56)
#define FIXTURE_DESCRIPTOR_ADDRESS_MASK 0x0003fffffffff000ULL
#define FIXTURE_PHYSICAL_ADDRESS_MASK   0x0000ffffffffffffULL
#define FIXTURE_PHYSICAL_PAGE_MASK      0x0000fffffffff000ULL
#define FIXTURE_STABLE_ATTR  (1ULL << 10)

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

enum fixture_event_kind {
	FIXTURE_EVENT_PTE_WRITE = 1,
	FIXTURE_EVENT_TLBI = 2,
	FIXTURE_EVENT_SYNC_EXEC = 3,
};

struct fixture_event {
	enum fixture_event_kind kind;
	long entry_index;
	uint64_t value;
	unsigned long address;
	unsigned int page_count;
};

struct fixture_mm {
	uint64_t ptes[FIXTURE_CONT_COUNT];
};

struct fixture_private_page {
	unsigned long pfn;
	unsigned char bytes[FIXTURE_PAGE_SIZE];
	int allocated;
	int freed;
};

/* Match the established ARM64 pt_regs scalar layout used by every profile. */
struct fixture_pt_regs {
	uint64_t x[31];
	uint64_t sp;
	uint64_t pc;
	uint64_t pstate;
	unsigned char tail[336 - 272];
};

_Static_assert(offsetof(struct fixture_pt_regs, x) == 0,
	       "pt_regs x offset");
_Static_assert(offsetof(struct fixture_pt_regs, sp) == 248,
	       "pt_regs sp offset");
_Static_assert(offsetof(struct fixture_pt_regs, pc) == 256,
	       "pt_regs pc offset");
_Static_assert(offsetof(struct fixture_pt_regs, pstate) == 264,
	       "pt_regs pstate offset");
_Static_assert(sizeof(struct fixture_pt_regs) == 336,
	       "pt_regs fixture size");

static struct fixture_mm fixture_mm;
static unsigned char fixture_original_page[FIXTURE_PAGE_SIZE];
static struct fixture_private_page fixture_private_pages[4];
static struct fixture_event fixture_events[256];
static size_t fixture_event_count;
static int fixture_atomic_depth;
static int fixture_sleep_calls;
static int fixture_mmap_read_depth;
static int fixture_mapping_validations;
static int fixture_mm_users_leases;
static int fixture_mm_count_leases;
static int fixture_mm_count_grabs;
static int fixture_mm_users_puts;
static int fixture_mm_count_drops;
static int fixture_fail_mm_count_grab;
static void *fixture_current_mm;
static int fixture_original_gets;
static int fixture_original_puts;
static unsigned long fixture_original_actual_pfn;
static int fixture_private_allocs;
static int fixture_private_frees;
static int fixture_rcu_depth;
static int fixture_rcu_begins;
static int fixture_rcu_ends;
static int fixture_pte_lock_depth;
static int fixture_pte_locks;
static int fixture_pte_unlocks;
static int fixture_fail_pte_lock;
static int fixture_pte_write_count;
static int fixture_fail_pte_write_n;
static int fixture_fail_pte_write_from;
static int fixture_nofault_reads;
static int fixture_nofault_writes;
static unsigned long fixture_private_pfn_base;
static struct neverc_krt_user_ptmap *fixture_reentrant_map;
static int fixture_reenter_on_write;
static int fixture_reentrant_result;
static int fixture_busy_during_mmap_begin;

static const struct neverc_krt_user_ptmap_test_geometry fixture_geometry = {
	.page_size = FIXTURE_PAGE_SIZE,
	.page_shift = FIXTURE_PAGE_SHIFT,
	.contiguous_entries = FIXTURE_CONT_COUNT,
	.descriptor_address_mask = FIXTURE_DESCRIPTOR_ADDRESS_MASK,
	.physical_address_mask = FIXTURE_PHYSICAL_ADDRESS_MASK,
	.physical_page_mask = FIXTURE_PHYSICAL_PAGE_MASK,
	.valid_mask = FIXTURE_PTE_VALID,
	.user_mask = FIXTURE_PTE_USER,
	.readonly_mask = FIXTURE_PTE_RDONLY,
	.write_mask = FIXTURE_PTE_WRITE,
	.uxn_mask = FIXTURE_PTE_UXN,
	.pxn_mask = FIXTURE_PTE_PXN,
	.ng_mask = FIXTURE_PTE_NG,
	.special_mask = FIXTURE_PTE_SPECIAL,
	.contiguous_mask = FIXTURE_PTE_CONT,
	.pt_regs_size = sizeof(struct fixture_pt_regs),
	.pt_regs_x = offsetof(struct fixture_pt_regs, x),
	.pt_regs_x_size = sizeof(((struct fixture_pt_regs *)0)->x),
	.pt_regs_sp = offsetof(struct fixture_pt_regs, sp),
	.pt_regs_pc = offsetof(struct fixture_pt_regs, pc),
	.pt_regs_pstate = offsetof(struct fixture_pt_regs, pstate),
	.pstate_mode_mask = 0xf,
	.pstate_user_mode = 0,
};

static uint64_t fixture_make_pte(unsigned long pfn, uint64_t attributes)
{
	return ((uint64_t)pfn << FIXTURE_PAGE_SHIFT) |
		(attributes & ~FIXTURE_DESCRIPTOR_ADDRESS_MASK);
}

static unsigned long fixture_pte_pfn(uint64_t pte)
{
	return (unsigned long)((pte & FIXTURE_DESCRIPTOR_ADDRESS_MASK) >>
			       FIXTURE_PAGE_SHIFT);
}

static uint64_t fixture_original_attributes(int contiguous)
{
	return FIXTURE_PTE_VALID | FIXTURE_PTE_USER | FIXTURE_PTE_RDONLY |
		FIXTURE_STABLE_ATTR | (contiguous ? FIXTURE_PTE_CONT : 0);
}

static void fixture_record_event(enum fixture_event_kind kind,
				 long entry_index, uint64_t value,
				 unsigned long address,
				 unsigned int page_count)
{
	assert(fixture_event_count < ARRAY_COUNT(fixture_events));
	fixture_events[fixture_event_count++] = (struct fixture_event){
		.kind = kind,
		.entry_index = entry_index,
		.value = value,
		.address = address,
		.page_count = page_count,
	};
}

static void fixture_clear_events(void)
{
	fixture_event_count = 0;
	memset(fixture_events, 0, sizeof(fixture_events));
}

static void fixture_reset(int contiguous)
{
	size_t i;

	memset(&fixture_mm, 0, sizeof(fixture_mm));
	for (i = 0; i < FIXTURE_CONT_COUNT; i++)
		fixture_mm.ptes[i] = fixture_make_pte(
			FIXTURE_ORIGINAL_PFN_BASE + i,
			fixture_original_attributes(contiguous));
	for (i = 0; i < sizeof(fixture_original_page); i++)
		fixture_original_page[i] = (unsigned char)(i ^ 0x5aU);
	memset(fixture_private_pages, 0, sizeof(fixture_private_pages));
	fixture_clear_events();
	fixture_atomic_depth = 0;
	fixture_sleep_calls = 0;
	fixture_mmap_read_depth = 0;
	fixture_mapping_validations = 0;
	fixture_mm_users_leases = 1;
	fixture_mm_count_leases = 0;
	fixture_mm_count_grabs = 0;
	fixture_mm_users_puts = 0;
	fixture_mm_count_drops = 0;
	fixture_fail_mm_count_grab = 0;
	fixture_current_mm = &fixture_mm;
	fixture_original_gets = 0;
	fixture_original_puts = 0;
	fixture_original_actual_pfn = FIXTURE_ORIGINAL_PFN;
	fixture_private_allocs = 0;
	fixture_private_frees = 0;
	fixture_rcu_depth = 0;
	fixture_rcu_begins = 0;
	fixture_rcu_ends = 0;
	fixture_pte_lock_depth = 0;
	fixture_pte_locks = 0;
	fixture_pte_unlocks = 0;
	fixture_fail_pte_lock = 0;
	fixture_pte_write_count = 0;
	fixture_fail_pte_write_n = 0;
	fixture_fail_pte_write_from = 0;
	fixture_nofault_reads = 0;
	fixture_nofault_writes = 0;
	fixture_private_pfn_base = 0x2200UL;
	fixture_reentrant_map = NULL;
	fixture_reenter_on_write = 0;
	fixture_reentrant_result = 0;
	fixture_busy_during_mmap_begin = -1;
}

static void fixture_atomic_begin(void)
{
	assert(fixture_atomic_depth == 0);
	fixture_atomic_depth++;
}

static void fixture_atomic_end(void)
{
	assert(fixture_atomic_depth == 1);
	fixture_atomic_depth--;
}

static int fixture_mmap_read_begin(void *mm)
{
	assert(mm == &fixture_mm);
	assert(fixture_atomic_depth == 0);
	assert(fixture_mmap_read_depth == 0);
	if (fixture_busy_during_mmap_begin < 0)
		fixture_busy_during_mmap_begin = neverc_krt_user_ptmap_busy();
	fixture_mmap_read_depth++;
	fixture_sleep_calls++;
	return 0;
}

static void fixture_mmap_read_end(void *mm)
{
	assert(mm == &fixture_mm);
	assert(fixture_atomic_depth == 0);
	assert(fixture_mmap_read_depth == 1);
	fixture_mmap_read_depth--;
}

static int fixture_mapping_validate(void *mm, unsigned long address)
{
	assert(mm == &fixture_mm);
	assert(address == FIXTURE_ADDRESS);
	assert(fixture_mmap_read_depth == 1);
	fixture_mapping_validations++;
	return 0;
}

static int fixture_mm_count_grab(void *mm)
{
	assert(mm == &fixture_mm);
	assert(fixture_atomic_depth == 0);
	assert(fixture_mm_users_leases == 1);
	assert(fixture_mm_count_leases == 0);
	if (fixture_fail_mm_count_grab)
		return -EAGAIN;
	fixture_mm_count_leases++;
	fixture_mm_count_grabs++;
	return 0;
}

static void fixture_mm_users_put(void *mm)
{
	assert(mm == &fixture_mm);
	assert(fixture_atomic_depth == 0);
	assert(fixture_mm_users_leases == 1);
	assert(fixture_mm_count_leases == 1);
	fixture_mm_users_leases--;
	fixture_mm_users_puts++;
}

static void fixture_mm_count_drop(void *mm)
{
	assert(mm == &fixture_mm);
	assert(fixture_atomic_depth == 0);
	assert(fixture_mm_users_leases == 0);
	assert(fixture_mm_count_leases == 1);
	fixture_mm_count_leases--;
	fixture_mm_count_drops++;
}

static int fixture_matches_current_mm(void *mm)
{
	assert(fixture_atomic_depth >= 0);
	return mm == fixture_current_mm ? 1 : 0;
}

static void fixture_assert_mm_lease_transferred(void)
{
	assert(fixture_mm_users_leases == 0);
	assert(fixture_mm_count_leases == 1);
	assert(fixture_mm_count_grabs == 1);
	assert(fixture_mm_users_puts == 1);
	assert(fixture_mm_count_drops == 0);
}

static void fixture_assert_mm_lease_released(void)
{
	assert(fixture_mm_users_leases == 0);
	assert(fixture_mm_count_leases == 0);
	assert(fixture_mm_count_grabs == 1);
	assert(fixture_mm_users_puts == 1);
	assert(fixture_mm_count_drops == 1);
}

static void fixture_assert_mm_lease_preserved(void)
{
	assert(fixture_mm_users_leases == 1);
	assert(fixture_mm_count_leases == 0);
	assert(fixture_mm_count_grabs == 0);
	assert(fixture_mm_users_puts == 0);
	assert(fixture_mm_count_drops == 0);
}

static int fixture_original_page_get(unsigned long expected_pfn,
				     unsigned long *actual_pfn,
				     void **page_address)
{
	assert(fixture_atomic_depth == 0);
	/* pin_user_pages_remote(..., locked=NULL) requires this lock and leaves
	 * it held.  The runtime compares the returned page identity with the
	 * descriptor snapshot before crossing the lock boundary. */
	assert(fixture_mmap_read_depth == 1);
	if (expected_pfn != FIXTURE_ORIGINAL_PFN ||
	    !actual_pfn || !page_address)
		return -EFAULT;
	fixture_original_gets++;
	*actual_pfn = fixture_original_actual_pfn;
	*page_address = fixture_original_page;
	return 0;
}

static void fixture_original_page_put(unsigned long pfn, void *page_address)
{
	assert(fixture_atomic_depth == 0);
	assert(fixture_mmap_read_depth == 0 || fixture_mmap_read_depth == 1);
	assert(pfn == fixture_original_actual_pfn);
	assert(page_address == fixture_original_page);
	fixture_original_puts++;
}

static int fixture_private_page_alloc(unsigned long *pfn, void **page_address)
{
	size_t i;

	assert(fixture_atomic_depth == 0);
	assert(pfn && page_address);
	fixture_sleep_calls++;
	for (i = 0; i < ARRAY_COUNT(fixture_private_pages); i++) {
		struct fixture_private_page *page = &fixture_private_pages[i];

		if (page->allocated)
			continue;
		page->allocated = 1;
		page->pfn = fixture_private_pfn_base + i;
		memset(page->bytes, 0, sizeof(page->bytes));
		*pfn = page->pfn;
		*page_address = page->bytes;
		fixture_private_allocs++;
		return 0;
	}
	return -ENOMEM;
}

static void fixture_private_page_free(unsigned long pfn, void *page_address)
{
	size_t i;

	assert(fixture_atomic_depth == 0);
	for (i = 0; i < ARRAY_COUNT(fixture_private_pages); i++) {
		struct fixture_private_page *page = &fixture_private_pages[i];

		if (page->pfn != pfn || page->bytes != page_address)
			continue;
		assert(page->allocated && !page->freed);
		page->freed = 1;
		fixture_private_frees++;
		return;
	}
	assert(!"free of unknown private page");
}

static int fixture_rcu_read_begin(void)
{
	fixture_rcu_begins++;
	fixture_rcu_depth++;
	return 0;
}

static void fixture_rcu_read_end(void)
{
	assert(fixture_rcu_depth == 1);
	assert(fixture_pte_lock_depth == 0);
	fixture_rcu_depth--;
	fixture_rcu_ends++;
}

static int fixture_pte_lock(
	void *mm, unsigned long address,
	struct neverc_krt_user_ptmap_test_pte_window *window)
{
	assert(mm == &fixture_mm);
	assert(address == FIXTURE_ADDRESS);
	assert(window);
	/* The runtime must keep RCU over the complete PTE pointer lifetime. */
	assert(fixture_rcu_depth == 1);
	assert(fixture_pte_lock_depth == 0);
	if (fixture_fail_pte_lock)
		return -EAGAIN;
	fixture_pte_locks++;
	fixture_pte_lock_depth++;
	window->entries = fixture_mm.ptes;
	window->entry_count = FIXTURE_CONT_COUNT;
	window->target_index = FIXTURE_TARGET_INDEX;
	window->group_address = FIXTURE_GROUP_ADDRESS;
	return 0;
}

static void fixture_pte_unlock(
	void *mm, struct neverc_krt_user_ptmap_test_pte_window *window)
{
	assert(mm == &fixture_mm);
	assert(window && window->entries == fixture_mm.ptes);
	assert(fixture_rcu_depth == 1);
	assert(fixture_pte_lock_depth == 1);
	fixture_pte_lock_depth--;
	fixture_pte_unlocks++;
}

static int fixture_pte_write(uint64_t *entry, uint64_t value)
{
	ptrdiff_t index;

	assert(fixture_rcu_depth == 1);
	assert(fixture_pte_lock_depth == 1);
	index = entry - fixture_mm.ptes;
	assert(index >= 0 && (size_t)index < FIXTURE_CONT_COUNT);
	fixture_pte_write_count++;
	if (fixture_fail_pte_write_n > 0 &&
	    fixture_pte_write_count == fixture_fail_pte_write_n)
		return -EIO;
	if (fixture_fail_pte_write_from > 0 &&
	    fixture_pte_write_count >= fixture_fail_pte_write_from)
		return -EIO;
	fixture_record_event(FIXTURE_EVENT_PTE_WRITE, (long)index, value, 0, 0);
	*entry = value;
	return 0;
}

static void fixture_tlbi_range(unsigned long address, unsigned int page_count)
{
	assert(fixture_rcu_depth == 1);
	assert(fixture_pte_lock_depth == 1);
	fixture_record_event(FIXTURE_EVENT_TLBI, -1, 0, address, page_count);
}

static void fixture_sync_exec_page(void *page_address, size_t size)
{
	size_t i;
	long page_index = -1;

	assert(size == FIXTURE_PAGE_SIZE);
	for (i = 0; i < ARRAY_COUNT(fixture_private_pages); i++) {
		if (fixture_private_pages[i].bytes == page_address)
			page_index = (long)i;
	}
	assert(page_index >= 0);
	fixture_record_event(FIXTURE_EVENT_SYNC_EXEC, page_index, 0, 0, 0);
}

static int fixture_nofault_read(void *destination, const void *source,
				size_t size)
{
	fixture_nofault_reads++;
	memcpy(destination, source, size);
	return 0;
}

static int fixture_nofault_write(void *destination, const void *source,
				 size_t size)
{
	if (fixture_reenter_on_write) {
		struct neverc_krt_user_ptmap_status status;

		fixture_reenter_on_write = 0;
		fixture_reentrant_result = neverc_krt_user_ptmap_query(
			fixture_reentrant_map, &status);
	}
	fixture_nofault_writes++;
	memcpy(destination, source, size);
	return 0;
}

static const struct neverc_krt_user_ptmap_test_backend fixture_backend = {
	.geometry = &fixture_geometry,
	.mmap_read_begin = fixture_mmap_read_begin,
	.mmap_read_end = fixture_mmap_read_end,
	.mapping_validate = fixture_mapping_validate,
	.mm_count_grab = fixture_mm_count_grab,
	.mm_users_put = fixture_mm_users_put,
	.mm_count_drop = fixture_mm_count_drop,
	.matches_current_mm = fixture_matches_current_mm,
	.original_page_get = fixture_original_page_get,
	.original_page_put = fixture_original_page_put,
	.private_page_alloc = fixture_private_page_alloc,
	.private_page_free = fixture_private_page_free,
	.rcu_read_begin = fixture_rcu_read_begin,
	.rcu_read_end = fixture_rcu_read_end,
	.pte_lock = fixture_pte_lock,
	.pte_unlock = fixture_pte_unlock,
	.pte_write = fixture_pte_write,
	.tlbi_range = fixture_tlbi_range,
	.sync_exec_page = fixture_sync_exec_page,
	.nofault_read = fixture_nofault_read,
	.nofault_write = fixture_nofault_write,
};

static struct neverc_krt_user_ptmap *fixture_install_map(
	unsigned int private_slots)
{
	struct neverc_krt_user_ptmap_install request = {
		.mm = &fixture_mm,
		.address = FIXTURE_ADDRESS + 37,
		.private_slots = private_slots,
	};
	struct neverc_krt_user_ptmap *map = NULL;

	assert(neverc_krt_user_ptmap_install(&request, &map) == 0);
	assert(map != NULL);
	return map;
}

static struct neverc_krt_user_ptmap_status fixture_query(
	struct neverc_krt_user_ptmap *map)
{
	struct neverc_krt_user_ptmap_status status;

	memset(&status, 0xa5, sizeof(status));
	assert(neverc_krt_user_ptmap_query(map, &status) == 0);
	return status;
}

static void fixture_read_slot(struct neverc_krt_user_ptmap *map,
			      enum neverc_krt_user_ptmap_slot slot,
			      size_t offset, void *destination, size_t size)
{
	memset(destination, 0xa5, size);
	assert(neverc_krt_user_ptmap_read(
		map, slot, offset, destination, size) == 0);
}

static struct fixture_private_page *fixture_private_page_by_pfn(
	unsigned long pfn)
{
	size_t i;

	for (i = 0; i < ARRAY_COUNT(fixture_private_pages); i++) {
		if (fixture_private_pages[i].allocated &&
		    fixture_private_pages[i].pfn == pfn)
			return &fixture_private_pages[i];
	}
	return NULL;
}

static void fixture_assert_exec_descriptor(uint64_t pte,
				   unsigned long expected_pfn)
{
	assert(fixture_pte_pfn(pte) == expected_pfn);
	assert(pte & FIXTURE_PTE_VALID);
	/* EL0 execute-only (XOM): AP[1]/PTE_USER is CLEAR so every EL0 data
	 * read/write faults, while PTE_UXN stays clear so EL0 may still
	 * fetch/execute — ARMv8.0 permits EL0 execute-only with AP[1]=0 &&
	 * UXN=0.  A same-page PC-relative data access is handled by the
	 * caller, not by leaving the page EL0 readable.  PTE_RDONLY +
	 * PTE_PXN complete the read-only, EL1-noexec view. */
	assert(!(pte & FIXTURE_PTE_USER));
	assert(pte & FIXTURE_PTE_RDONLY);
	assert(!(pte & FIXTURE_PTE_UXN));
	assert(pte & FIXTURE_PTE_PXN);
	assert(pte & FIXTURE_PTE_NG);
	assert(pte & FIXTURE_PTE_SPECIAL);
	assert(!(pte & FIXTURE_PTE_CONT));
	assert(pte & FIXTURE_STABLE_ATTR);
}

static void fixture_assert_rw_nx_descriptor(uint64_t pte,
				    unsigned long expected_pfn)
{
	assert(fixture_pte_pfn(pte) == expected_pfn);
	assert(pte & FIXTURE_PTE_VALID);
	assert(pte & FIXTURE_PTE_USER);
	assert(!(pte & FIXTURE_PTE_RDONLY));
	assert(pte & FIXTURE_PTE_WRITE);
	assert(pte & FIXTURE_PTE_UXN);
	assert(pte & FIXTURE_PTE_PXN);
	assert(pte & FIXTURE_PTE_NG);
	assert(pte & FIXTURE_PTE_SPECIAL);
	assert(!(pte & FIXTURE_PTE_CONT));
	assert(pte & FIXTURE_STABLE_ATTR);
}

static size_t fixture_relevant_events(struct fixture_event *out,
				      size_t *source_positions)
{
	size_t input;
	size_t output = 0;

	for (input = 0; input < fixture_event_count; input++) {
		if (fixture_events[input].kind == FIXTURE_EVENT_SYNC_EXEC)
			continue;
		out[output] = fixture_events[input];
		if (source_positions)
			source_positions[output] = input;
		output++;
	}
	return output;
}

static void fixture_assert_exec_sync_before(size_t target_make_position)
{
	size_t i;
	int found = 0;

	for (i = 0; i < target_make_position; i++) {
		if (fixture_events[i].kind == FIXTURE_EVENT_SYNC_EXEC)
			found = 1;
	}
	assert(found);
}

static void fixture_assert_contiguous_bbm(uint64_t expected_target)
{
	struct fixture_event relevant[64];
	size_t source_positions[64];
	int neighbor_seen[FIXTURE_CONT_COUNT] = {0};
	size_t count;
	size_t i;

	count = fixture_relevant_events(relevant, source_positions);
	/* 16 breaks, group TLBI, 15 neighbor makes, target make, final TLBI. */
	assert(count == 34);
	for (i = 0; i < FIXTURE_CONT_COUNT; i++) {
		assert(relevant[i].kind == FIXTURE_EVENT_PTE_WRITE);
		assert(relevant[i].entry_index == (long)i);
		assert(relevant[i].value == 0);
	}
	assert(relevant[16].kind == FIXTURE_EVENT_TLBI);
	assert(relevant[16].address == FIXTURE_GROUP_ADDRESS);
	assert(relevant[16].page_count == FIXTURE_CONT_COUNT);
	for (i = 17; i < 32; i++) {
		long index = relevant[i].entry_index;

		assert(relevant[i].kind == FIXTURE_EVENT_PTE_WRITE);
		assert(index >= 0 && index < (long)FIXTURE_CONT_COUNT);
		assert(index != FIXTURE_TARGET_INDEX);
		assert(!neighbor_seen[index]);
		neighbor_seen[index] = 1;
		assert(relevant[i].value & FIXTURE_PTE_VALID);
		assert(!(relevant[i].value & FIXTURE_PTE_CONT));
		assert(fixture_pte_pfn(relevant[i].value) ==
		       FIXTURE_ORIGINAL_PFN_BASE + (unsigned long)index);
	}
	assert(relevant[32].kind == FIXTURE_EVENT_PTE_WRITE);
	assert(relevant[32].entry_index == FIXTURE_TARGET_INDEX);
	assert(relevant[32].value == expected_target);
	assert(relevant[33].kind == FIXTURE_EVENT_TLBI);
	assert(relevant[33].address == FIXTURE_ADDRESS);
	assert(relevant[33].page_count == 1);
	fixture_assert_exec_sync_before(source_positions[32]);
}

static void fixture_assert_single_page_bbm(uint64_t expected_target)
{
	struct fixture_event relevant[16];
	size_t count = fixture_relevant_events(relevant, NULL);

	assert(count == 4);
	assert(relevant[0].kind == FIXTURE_EVENT_PTE_WRITE);
	assert(relevant[0].entry_index == FIXTURE_TARGET_INDEX);
	assert(relevant[0].value == 0);
	assert(relevant[1].kind == FIXTURE_EVENT_TLBI);
	assert(relevant[1].address == FIXTURE_ADDRESS);
	assert(relevant[1].page_count == 1);
	assert(relevant[2].kind == FIXTURE_EVENT_PTE_WRITE);
	assert(relevant[2].entry_index == FIXTURE_TARGET_INDEX);
	assert(relevant[2].value == expected_target);
	assert(relevant[3].kind == FIXTURE_EVENT_TLBI);
	assert(relevant[3].address == FIXTURE_ADDRESS);
	assert(relevant[3].page_count == 1);
}

static void fixture_assert_permission_only_make(uint64_t expected_target)
{
	struct fixture_event relevant[8];
	size_t count = fixture_relevant_events(relevant, NULL);

	assert(count == 2);
	assert(relevant[0].kind == FIXTURE_EVENT_PTE_WRITE);
	assert(relevant[0].entry_index == FIXTURE_TARGET_INDEX);
	assert(relevant[0].value == expected_target);
	assert(relevant[1].kind == FIXTURE_EVENT_TLBI);
	assert(relevant[1].address == FIXTURE_ADDRESS);
	assert(relevant[1].page_count == 1);
}

static void fixture_assert_balanced_atomic_locks(void)
{
	assert(fixture_rcu_depth == 0);
	assert(fixture_pte_lock_depth == 0);
	assert(fixture_rcu_begins == fixture_rcu_ends);
	assert(fixture_pte_locks == fixture_pte_unlocks);
}

static void check_active_hook_views_copy_bbm_and_ownership(void)
{
	static const unsigned char patch[] = {0x1f, 0x20, 0x03, 0xd5};
	static const unsigned char changed[] = {0xde, 0xad, 0xbe, 0xef};
	struct neverc_krt_user_ptmap *map;
	struct neverc_krt_user_ptmap_status status;
	struct fixture_private_page *exec_page;
	struct fixture_private_page *rw_page;
	unsigned char page_copy[FIXTURE_PAGE_SIZE];
	unsigned char bytes[sizeof(patch)];
	uint64_t target;
	unsigned long exec_pfn;
	unsigned long rw_pfn;
	int pte_locks_before_sync;
	int sleep_calls_after_install;

	fixture_reset(1);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	fixture_assert_mm_lease_transferred();
	assert(neverc_krt_user_ptmap_matches_mm(map, &fixture_mm) == 1);
	assert(neverc_krt_user_ptmap_matches_mm(
		map, fixture_private_pages) == 0);
	assert(neverc_krt_user_ptmap_matches_mm(map, NULL) == -EINVAL);
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_matches_current_mm(map) == 1);
	/* A recycled TGID can reach the same hook entry with a different mm. */
	fixture_current_mm = fixture_private_pages;
	assert(neverc_krt_user_ptmap_matches_current_mm(map) == 0);
	fixture_current_mm = &fixture_mm;
	fixture_atomic_end();
	assert(fixture_mapping_validations == 1);
	assert(fixture_original_gets == 1);
	assert(fixture_private_allocs == 2);
	assert(fixture_private_frees == 0);
	assert(fixture_original_puts == 0);
	status = fixture_query(map);
	assert(status.address == FIXTURE_ADDRESS);
	assert(status.page_size == FIXTURE_PAGE_SIZE);
	assert(status.view == NEVERC_KRT_USER_PTMAP_ORIGINAL);
	assert(status.private_slots == NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	assert(status.current_matches == 1);
	assert(status.private_mapped == 0);

	sleep_calls_after_install = fixture_sleep_calls;
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
			 0, page_copy, sizeof(page_copy));
	assert(memcmp(page_copy, fixture_original_page, sizeof(page_copy)) == 0);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
			 0, page_copy, sizeof(page_copy));
	assert(memcmp(page_copy, fixture_original_page, sizeof(page_copy)) == 0);
	fixture_clear_events();
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC, 64,
		patch, sizeof(patch)) == 0);
	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	pte_locks_before_sync = fixture_pte_locks;
	assert(neverc_krt_user_ptmap_sync_exec(map) == 0);
	assert(fixture_event_count == 1);
	assert(fixture_events[0].kind == FIXTURE_EVENT_SYNC_EXEC);
	assert(fixture_mm.ptes[FIXTURE_TARGET_INDEX] == target);
	assert(fixture_pte_locks == pte_locks_before_sync);
	assert(neverc_krt_user_ptmap_sync_exec(map) == 0);
	assert(fixture_event_count == 1);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_ORIGINAL);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
			 64, bytes, sizeof(bytes));
	assert(memcmp(bytes, patch, sizeof(bytes)) == 0);
	/* Bounds failures are exact and do not alter the destination. */
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
		FIXTURE_PAGE_SIZE - 1, patch, sizeof(patch)) == -EINVAL);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
			 64, bytes, sizeof(bytes));
	assert(memcmp(bytes, patch, sizeof(bytes)) == 0);
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_ORIGINAL,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	fixture_atomic_end();
	assert(fixture_sleep_calls == sleep_calls_after_install);

	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	exec_pfn = fixture_pte_pfn(target);
	exec_page = fixture_private_page_by_pfn(exec_pfn);
	assert(exec_page != NULL);
	assert(memcmp(exec_page->bytes + 64, patch, sizeof(patch)) == 0);
	fixture_assert_exec_descriptor(target, exec_pfn);
	fixture_assert_contiguous_bbm(target);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY);
	assert(status.current_matches == 1);
	assert(status.private_mapped == 1);

	fixture_clear_events();
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY,
		NEVERC_KRT_USER_PTMAP_USER_RW_NX) == 0);
	fixture_atomic_end();
	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	rw_pfn = fixture_pte_pfn(target);
	rw_page = fixture_private_page_by_pfn(rw_pfn);
	assert(rw_page != NULL && rw_page != exec_page);
	fixture_assert_rw_nx_descriptor(target, rw_pfn);
	fixture_assert_single_page_bbm(target);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_USER_RW_NX);
	assert(status.private_mapped == 1);

	/* A same-page store changes the USER_RW private slot, then the caller
	 * can atomically clone it back to EXEC and reapply patches. */
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW, 128,
		changed, sizeof(changed)) == 0);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
		NEVERC_KRT_USER_PTMAP_SLOT_USER_RW) == 0);
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC, 64,
		patch, sizeof(patch)) == 0);
	fixture_clear_events();
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_USER_RW_NX,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	fixture_atomic_end();
	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	assert(fixture_pte_pfn(target) == exec_pfn);
	fixture_assert_exec_descriptor(target, exec_pfn);
	fixture_assert_single_page_bbm(target);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
			 128, bytes, sizeof(bytes));
	assert(memcmp(bytes, changed, sizeof(bytes)) == 0);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
			 64, bytes, sizeof(bytes));
	assert(memcmp(bytes, patch, sizeof(bytes)) == 0);

	fixture_clear_events();
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	fixture_atomic_end();
	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	assert(fixture_pte_pfn(target) == FIXTURE_ORIGINAL_PFN);
	assert(!(target & FIXTURE_PTE_CONT));
	assert((target & ~FIXTURE_DESCRIPTOR_ADDRESS_MASK) ==
	       (fixture_original_attributes(0) &
		~FIXTURE_DESCRIPTOR_ADDRESS_MASK));
	fixture_assert_single_page_bbm(target);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_ORIGINAL);
	assert(status.current_matches == 1);
	assert(status.private_mapped == 0);

	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	assert(fixture_private_frees == 2);
	assert(fixture_original_puts == 1);
	fixture_assert_mm_lease_released();
	fixture_assert_balanced_atomic_locks();
}

static void check_exec_only_uses_original_exec_and_private_data(void)
{
	static const unsigned char fake[] = {
		0x77, 0x77, 0x77, 0x77, 0x88, 0x88, 0x88, 0x88,
	};
	struct neverc_krt_user_ptmap *map;
	struct neverc_krt_user_ptmap_status status;
	uint64_t target;
	unsigned long rw_pfn;
	unsigned char readback[sizeof(fake)];

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_USER_RW);
	assert(fixture_private_allocs == 1);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW, 0,
		fake, sizeof(fake)) == 0);
	/* EXEC is a referenced original backing in EXEC_ONLY, never writable. */
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC, 0,
		fake, sizeof(fake)) == -EPERM);

	fixture_clear_events();
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_ORIGINAL,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	fixture_atomic_end();
	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	assert(fixture_pte_pfn(target) == FIXTURE_ORIGINAL_PFN);
	fixture_assert_exec_descriptor(target, FIXTURE_ORIGINAL_PFN);
	fixture_assert_permission_only_make(target);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY);
	assert(status.current_matches == 1);
	assert(status.private_mapped == 0);

	fixture_clear_events();
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY,
		NEVERC_KRT_USER_PTMAP_USER_RW_NX) == 0);
	fixture_atomic_end();
	target = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	rw_pfn = fixture_pte_pfn(target);
	assert(fixture_private_page_by_pfn(rw_pfn) != NULL);
	fixture_assert_rw_nx_descriptor(target, rw_pfn);
	fixture_assert_single_page_bbm(target);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
			 0, readback, sizeof(readback));
	assert(memcmp(readback, fake, sizeof(fake)) == 0);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_USER_RW_NX);
	assert(status.private_mapped == 1);

	/* Returning to EXEC exposes the original PFN with execute-only
	 * permissions, not the fake-data page and not a passive mapping. */
	fixture_clear_events();
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_USER_RW_NX,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	fixture_atomic_end();
	assert(fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]) == FIXTURE_ORIGINAL_PFN);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_ORIGINAL);
	assert(status.private_mapped == 0);
	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	assert(fixture_private_frees == 1);
	assert(fixture_original_puts == 1);
	fixture_assert_mm_lease_released();
	fixture_assert_balanced_atomic_locks();
}

static void check_expected_descriptor_rejects_stale_and_alien_mapping(void)
{
	struct neverc_krt_user_ptmap *map;
	struct neverc_krt_user_ptmap_status status;
	uint64_t original;

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	original = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	fixture_clear_events();
	/* The caller supplies a logical expected view, never a PFN.  The runtime
	 * compares its private full descriptor and rejects a stale state. */
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY,
		NEVERC_KRT_USER_PTMAP_USER_RW_NX) == -EAGAIN);
	assert(fixture_event_count == 0);
	assert(fixture_mm.ptes[FIXTURE_TARGET_INDEX] == original);

	/* Simulate munmap/remap replacing the live descriptor behind the handle. */
	fixture_mm.ptes[FIXTURE_TARGET_INDEX] = fixture_make_pte(
		FIXTURE_ALIEN_PFN, fixture_original_attributes(0));
	fixture_clear_events();
	status = fixture_query(map);
	assert(status.current_matches == 0);
	assert(status.private_mapped == 0);
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_ORIGINAL,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == -EAGAIN);
	assert(fixture_event_count == 0);
	assert(fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]) == FIXTURE_ALIEN_PFN);

	/* An external non-private mapping is safe to finalize without overwriting
	 * it; only the runtime-owned pages and references are released. */
	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	assert(fixture_private_frees == 2);
	assert(fixture_original_puts == 1);
	fixture_assert_mm_lease_released();
	assert(fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]) == FIXTURE_ALIEN_PFN);
	fixture_assert_balanced_atomic_locks();
}

static void check_restore_failure_keeps_mapped_private_page_owned(void)
{
	struct neverc_krt_user_ptmap *map;
	struct neverc_krt_user_ptmap_status status;
	unsigned long private_pfn;

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_ORIGINAL,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	private_pfn = fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]);
	assert(fixture_private_page_by_pfn(private_pfn) != NULL);

	fixture_fail_pte_lock = 1;
	fixture_clear_events();
	fixture_atomic_begin();
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == -EAGAIN);
	fixture_atomic_end();
	assert(fixture_event_count == 0);
	assert(fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]) == private_pfn);
	assert(fixture_private_frees == 0);
	assert(fixture_original_puts == 0);
	fixture_assert_mm_lease_transferred();

	/* destroy must conservatively retain ownership when its own locked query
	 * cannot prove that no private PFN is mapped. */
	assert(neverc_krt_user_ptmap_destroy(&map) == -EBUSY);
	assert(map != NULL);
	assert(fixture_private_frees == 0);
	assert(fixture_original_puts == 0);
	fixture_assert_mm_lease_transferred();
	assert(fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]) == private_pfn);

	fixture_fail_pte_lock = 0;
	status = fixture_query(map);
	assert(status.current_matches == 1);
	assert(status.private_mapped == 1);
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_ORIGINAL);
	assert(status.private_mapped == 0);
	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	assert(fixture_private_frees == 2);
	assert(fixture_original_puts == 1);
	fixture_assert_mm_lease_released();
	assert(neverc_krt_user_ptmap_destroy(&map) == -EINVAL);
	fixture_assert_mm_lease_released();
	fixture_assert_balanced_atomic_locks();
}

static void check_partial_pte_mutation_rolls_back_or_blocks_destroy(void)
{
	struct neverc_krt_user_ptmap *map;
	struct neverc_krt_user_ptmap_status status;
	unsigned long private_pfn;
	uint64_t exec_descriptor;

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_ORIGINAL,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	private_pfn = fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]);
	exec_descriptor = fixture_mm.ptes[FIXTURE_TARGET_INDEX];

	/* Succeed the BBM clear, fail only the new descriptor, then roll back. */
	fixture_pte_write_count = 0;
	fixture_fail_pte_write_n = 2;
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == -EIO);
	fixture_fail_pte_write_n = 0;
	assert(fixture_mm.ptes[FIXTURE_TARGET_INDEX] == exec_descriptor);
	status = fixture_query(map);
	assert(status.view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY);
	assert(status.current_matches == 1);
	assert(status.private_mapped == 1);
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_EXEC,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	assert(neverc_krt_user_ptmap_transition(
		map, NEVERC_KRT_USER_PTMAP_ORIGINAL,
		NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == 0);
	private_pfn = fixture_pte_pfn(
		fixture_mm.ptes[FIXTURE_TARGET_INDEX]);
	(void)private_pfn;

	/* Clear succeeds; new descriptor and rollback both fail. */
	fixture_pte_write_count = 0;
	fixture_fail_pte_write_from = 2;
	assert(neverc_krt_user_ptmap_restore(
		map, NEVERC_KRT_USER_PTMAP_EXEC_ONLY) == -EIO);
	fixture_fail_pte_write_from = 0;
	assert(fixture_mm.ptes[FIXTURE_TARGET_INDEX] == 0);
	status = fixture_query(map);
	assert(status.current_matches == 0);
	assert(status.private_mapped == 0);
	assert(neverc_krt_user_ptmap_destroy(&map) == -EBUSY);
	assert(map != NULL);
	assert(neverc_krt_user_ptmap_test_release(&map) == 0);
	assert(map == NULL);
	fixture_assert_balanced_atomic_locks();
}

/*
 * Minimal unsigned LDR/STR tracer.  The runtime supplies opaque register
 * snapshots and atomic slot byte access; instruction decode stays with the
 * caller.  This fixture covers scalar unsigned-offset LDR/STR and XZR
 * semantics.
 */
static int fixture_emulate_unsigned_ldst(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot data_slot,
	uint32_t instruction, unsigned long fault_address, int is_write,
	struct neverc_krt_user_fault_regs *registers)
{
	struct neverc_krt_user_ptmap_status status;
	unsigned int rt = instruction & 0x1fU;
	unsigned int size_log2 = instruction >> 30;
	unsigned int opc = (instruction >> 22) & 0x3U;
	size_t access_size;
	size_t offset;
	uint64_t value = 0;
	int rc;

	if (!map || !registers ||
	    (instruction & 0x3b000000U) != 0x39000000U)
		return -EOPNOTSUPP;
	if (size_log2 > 3)
		return -EOPNOTSUPP;
	access_size = (size_t)1U << size_log2;
	if ((opc == 0 && !is_write) || (opc == 1 && is_write) || opc > 1)
		return -EOPNOTSUPP;
	rc = neverc_krt_user_ptmap_query(map, &status);
	if (rc)
		return rc;
	if (fault_address < status.address ||
	    fault_address - status.address > status.page_size - access_size)
		return -EFAULT;
	offset = (size_t)(fault_address - status.address);

	if (is_write) {
		value = rt < 31 ? registers->x[rt] : 0;
		rc = neverc_krt_user_ptmap_write(
			map, data_slot, offset, &value, access_size);
	} else {
		rc = neverc_krt_user_ptmap_read(
			map, data_slot, offset, &value, access_size);
		if (!rc && rt < 31)
			registers->x[rt] = value;
	}
	if (rc)
		return rc;
	registers->pc += 4;
	return 0;
}

static void check_atomic_fault_register_snapshot_slot_emulation_and_commit(void)
{
	struct neverc_krt_user_ptmap *map;
	struct fixture_pt_regs opaque = {0};
	struct neverc_krt_user_fault_regs registers;
	struct neverc_krt_user_fault_regs before_unsupported;
	uint64_t value = 0x8877665544332211ULL;
	uint64_t readback = ~0ULL;
	int sleep_calls_after_install;

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	assert(neverc_krt_user_ptmap_copy(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL) == 0);
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		0x180, &value, sizeof(value)) == 0);
	opaque.x[1] = 0x1111;
	opaque.x[4] = 0x4444;
	opaque.sp = 0x7fff0000;
	opaque.pc = FIXTURE_ADDRESS + 0x100;
	opaque.pstate = 0; /* EL0t */
	sleep_calls_after_install = fixture_sleep_calls;

	fixture_atomic_begin();
	memset(&registers, 0xa5, sizeof(registers));
	assert(neverc_krt_user_fault_regs_snapshot(
		&opaque, &registers) == 0);
	assert(registers.x[1] == opaque.x[1]);
	assert(registers.x[4] == opaque.x[4]);
	assert(registers.sp == opaque.sp);
	assert(registers.pc == opaque.pc);
	assert(registers.pstate == opaque.pstate);
	assert(registers.user_mode == 1);

	/* LDR X4, [Xn, #imm] -- FAR supplies the already-decoded address. */
	assert(fixture_emulate_unsigned_ldst(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		0xf9400004U, FIXTURE_ADDRESS + 0x180, 0,
		&registers) == 0);
	assert(registers.x[4] == value);
	assert(registers.pc == opaque.pc + 4);
	assert(neverc_krt_user_fault_regs_commit(
		&opaque, &registers) == 0);
	assert(opaque.x[4] == value);
	assert(opaque.pc == FIXTURE_ADDRESS + 0x104);

	/* STR XZR stores zero; transfer register 31 is not SP. */
	assert(fixture_emulate_unsigned_ldst(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		0xf900001fU, FIXTURE_ADDRESS + 0x188, 1,
		&registers) == 0);
	assert(registers.sp == 0x7fff0000);
	fixture_read_slot(map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
			 0x188, &readback, sizeof(readback));
	assert(readback == 0);

	/* Unsupported decoder input is a caller decision and changes nothing. */
	before_unsupported = registers;
	assert(fixture_emulate_unsigned_ldst(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		0xd503201fU, FIXTURE_ADDRESS + 0x180, 0,
		&registers) == -EOPNOTSUPP);
	assert(memcmp(&registers, &before_unsupported,
		      sizeof(registers)) == 0);
	fixture_atomic_end();
	assert(fixture_sleep_calls == sleep_calls_after_install);
	assert(fixture_nofault_reads > 0);
	assert(fixture_nofault_writes > 0);

	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	fixture_assert_balanced_atomic_locks();
}

static void check_same_handle_reentry_fails_busy(void)
{
	static const unsigned char value[] = {0xaa, 0xbb, 0xcc, 0xdd};
	struct neverc_krt_user_ptmap *map;

	fixture_reset(0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	fixture_reentrant_map = map;
	fixture_reenter_on_write = 1;
	assert(neverc_krt_user_ptmap_write(
		map, NEVERC_KRT_USER_PTMAP_SLOT_USER_RW,
		0, value, sizeof(value)) == 0);
	assert(fixture_reenter_on_write == 0);
	assert(fixture_reentrant_result == -EBUSY);
	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	fixture_assert_mm_lease_released();
}

static void check_active_maps_report_busy_until_destroy(void)
{
	struct neverc_krt_user_ptmap *map;

	fixture_reset(0);
	assert(neverc_krt_user_ptmap_busy() == 0);
	map = fixture_install_map(NEVERC_KRT_USER_PTMAP_PRIVATE_ALL);
	assert(fixture_busy_during_mmap_begin == 1);
	assert(neverc_krt_user_ptmap_busy() == 1);
	assert(neverc_krt_user_ptmap_destroy(&map) == 0);
	assert(map == NULL);
	assert(neverc_krt_user_ptmap_busy() == 0);
	fixture_assert_mm_lease_released();
	fixture_assert_balanced_atomic_locks();
}

static void check_invalid_install_is_transactional(void)
{
	struct neverc_krt_user_ptmap_install request;
	struct neverc_krt_user_ptmap *map =
		(struct neverc_krt_user_ptmap *)(uintptr_t)0x1;
	uint64_t original;

	fixture_reset(0);
	original = fixture_mm.ptes[FIXTURE_TARGET_INDEX];
	memset(&request, 0, sizeof(request));
	request.mm = &fixture_mm;
	request.address = FIXTURE_ADDRESS;
	request.private_slots = NEVERC_KRT_USER_PTMAP_PRIVATE_EXEC;
	assert(neverc_krt_user_ptmap_install(&request, &map) == -EINVAL);
	assert(map == NULL);
	assert(neverc_krt_user_ptmap_busy() == 0);
	assert(fixture_mm.ptes[FIXTURE_TARGET_INDEX] == original);
	assert(fixture_private_allocs == 0);
	assert(fixture_private_frees == 0);
	fixture_assert_mm_lease_preserved();
	assert(fixture_original_gets == 0 && fixture_original_puts == 0);
}

static void check_pinned_page_must_match_descriptor_identity(void)
{
	struct neverc_krt_user_ptmap_install request = {
		.mm = &fixture_mm,
		.address = FIXTURE_ADDRESS,
		.private_slots = NEVERC_KRT_USER_PTMAP_PRIVATE_ALL,
	};
	struct neverc_krt_user_ptmap *map = NULL;

	fixture_reset(0);
	fixture_original_actual_pfn = FIXTURE_ALIEN_PFN;
	assert(neverc_krt_user_ptmap_install(&request, &map) == -EAGAIN);
	assert(map == NULL);
	assert(neverc_krt_user_ptmap_busy() == 0);
	assert(fixture_mmap_read_depth == 0);
	assert(fixture_original_gets == 1);
	assert(fixture_original_puts == 1);
	assert(fixture_private_allocs == 0);
	assert(fixture_private_frees == 0);
	fixture_assert_mm_lease_preserved();
}

static void check_private_page_must_fit_physical_address_width(void)
{
	struct neverc_krt_user_ptmap_install request = {
		.mm = &fixture_mm,
		.address = FIXTURE_ADDRESS,
		.private_slots = NEVERC_KRT_USER_PTMAP_PRIVATE_ALL,
	};
	struct neverc_krt_user_ptmap *map = NULL;

	fixture_reset(0);
	/* Encodable by the descriptor mask (bit 48), forbidden by PA48. */
	fixture_private_pfn_base =
		(unsigned long)(FIXTURE_PHYSICAL_PAGE_MASK >>
			      FIXTURE_PAGE_SHIFT) + 1UL;
	assert(neverc_krt_user_ptmap_install(&request, &map) == -EFAULT);
	assert(map == NULL);
	assert(neverc_krt_user_ptmap_busy() == 0);
	assert(fixture_private_allocs == 1);
	assert(fixture_private_frees == 1);
	assert(fixture_original_gets == 1 && fixture_original_puts == 1);
	fixture_assert_mm_lease_preserved();
}

static void check_mm_count_grab_failure_preserves_input_lease(void)
{
	struct neverc_krt_user_ptmap_install request = {
		.mm = &fixture_mm,
		.address = FIXTURE_ADDRESS,
		.private_slots = NEVERC_KRT_USER_PTMAP_PRIVATE_ALL,
	};
	struct neverc_krt_user_ptmap *map = NULL;

	fixture_reset(0);
	fixture_fail_mm_count_grab = 1;
	assert(neverc_krt_user_ptmap_install(&request, &map) == -EAGAIN);
	assert(map == NULL);
	assert(neverc_krt_user_ptmap_busy() == 0);
	fixture_assert_mm_lease_preserved();
	assert(fixture_private_allocs == 2);
	assert(fixture_private_frees == 2);
	assert(fixture_original_gets == 1);
	assert(fixture_original_puts == 1);
}

static void check_profile_policy_is_exact(void)
{
	static const unsigned int profiles[] = {
		510, 51013, 515, 51514, 601, 606, 612, 618,
	};
	struct neverc_krt_user_ptmap_test_profile_policy policy;
	size_t i;

	for (i = 0; i < ARRAY_COUNT(profiles); i++) {
		unsigned int linux_major;
		unsigned int linux_minor;
		int follow_pte;
		int pin_with_vmas;
		int get_free_pages;
		int desc48;
		unsigned int rcu_release;

		assert(neverc_krt_user_ptmap_test_profile_identity(
			       profiles[i], &linux_major, &linux_minor) == 0);
		follow_pte = linux_major == 5 ||
			     (linux_major == 6 && linux_minor <= 6);
		pin_with_vmas = linux_major == 5 ||
				(linux_major == 6 && linux_minor == 1);
		get_free_pages = follow_pte;
		desc48 = follow_pte;
		rcu_release = linux_major == 6 && linux_minor >= 6;
		memset(&policy, 0xa5, sizeof(policy));
		assert(neverc_krt_user_ptmap_test_profile_policy(
			profiles[i], &policy) == 0);
		assert(policy.physical_address_mask ==
		       FIXTURE_PHYSICAL_ADDRESS_MASK);
		assert(policy.physical_page_mask == FIXTURE_PHYSICAL_PAGE_MASK);
		assert(policy.pte_route ==
		       (follow_pte ?
			NEVERC_KRT_USER_PTMAP_TEST_FOLLOW_PTE :
			NEVERC_KRT_USER_PTMAP_TEST_PTE_OFFSET_MAP_LOCK));
		assert(policy.pin_abi ==
		       (pin_with_vmas ?
			NEVERC_KRT_USER_PTMAP_TEST_PIN_WITH_VMAS :
			NEVERC_KRT_USER_PTMAP_TEST_PIN_WITHOUT_VMAS));
		assert(policy.alloc_abi ==
		       (get_free_pages ?
			NEVERC_KRT_USER_PTMAP_TEST_GET_FREE_PAGES :
			NEVERC_KRT_USER_PTMAP_TEST_GET_FREE_PAGES_NOPROF));
		assert(policy.descriptor_address_mask ==
		       (desc48 ?
			FIXTURE_PHYSICAL_PAGE_MASK :
			FIXTURE_DESCRIPTOR_ADDRESS_MASK));
		assert(policy.pte_release_internal_rcu == rcu_release);
	}
	memset(&policy, 0xa5, sizeof(policy));
	assert(neverc_krt_user_ptmap_test_profile_policy(0, &policy) ==
	       -EOPNOTSUPP);
	assert(policy.descriptor_address_mask == 0);
}

static void check_runtime_gate_fails_closed_on_tcr_and_kcfi(void)
{
	static const uint32_t classic_tags[] = {
		0x14f277e5U, 0x485e012fU, 0xfee925afU, 0x3086ada5U,
		0xf2d356caU, 0x31f85444U, 0xaa91dafaU, 0xa540670cU,
		0x2b60aac7U, 0x51aeb208U, 0x8e0b794cU, 0x1d7a58e3U,
	};
	static const uint32_t normalized_tags[] = {
		0x53c2bd3fU, 0x62f58ccbU, 0x35183cccU, 0x3c5d8714U,
		0xc6ba6ed7U, 0x7d66fa54U, 0x9f823190U, 0xe5c47d60U,
		0x9fa57c4cU, 0xcef73528U, 0xa65f2b02U, 0xb57726fcU,
	};
	uint32_t bad_tags[ARRAY_COUNT(classic_tags)];
	uint64_t tcr = 25ULL | (5ULL << 32); /* 4K TG0, VA39, PA48. */

	assert(neverc_krt_user_ptmap_test_runtime_gate(
		510, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_DISABLED,
		NULL, 0) == 0);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags)) == 0);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		612, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_NORMALIZED,
		normalized_tags, ARRAY_COUNT(normalized_tags)) == 0);

	/* TG0, T0SZ, IPS, profile and KCFI mode are independent fail-closed
	 * observations; no major/minor compatibility guess is accepted. */
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr | (1ULL << 14),
		NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags)) == -EOPNOTSUPP);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, (tcr & ~0x3fULL) | 24ULL,
		NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags)) == -EOPNOTSUPP);
	/* IPS selects the PA size: values ≤48-bit (IPS 0..5) are accepted since
	 * their PFNs fit the mask (a smaller-PA COMPAT device is fine); only
	 * 52-bit (IPS 6/7, needs LPA2 OA[51:50]) is fail-closed. */
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr ^ (1ULL << 32),
		NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags)) == 0);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr | (2ULL << 32),
		NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags)) == -EOPNOTSUPP);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		999, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags)) == -EOPNOTSUPP);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_NORMALIZED,
		classic_tags, ARRAY_COUNT(classic_tags)) == -EOPNOTSUPP);

	memcpy(bad_tags, classic_tags, sizeof(bad_tags));
	bad_tags[9] ^= 1U;
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		bad_tags, ARRAY_COUNT(bad_tags)) == -EOPNOTSUPP);
	assert(neverc_krt_user_ptmap_test_runtime_gate(
		601, tcr, NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC,
		classic_tags, ARRAY_COUNT(classic_tags) - 1U) ==
	       -EOPNOTSUPP);
}

int main(void)
{
	assert(neverc_krt_user_ptmap_test_bind_backend(&fixture_backend) == 0);
	assert(neverc_krt_user_ptmap_available() == 1);
	check_profile_policy_is_exact();
	check_runtime_gate_fails_closed_on_tcr_and_kcfi();
	check_invalid_install_is_transactional();
	check_pinned_page_must_match_descriptor_identity();
	check_private_page_must_fit_physical_address_width();
	check_mm_count_grab_failure_preserves_input_lease();
	check_active_hook_views_copy_bbm_and_ownership();
	check_exec_only_uses_original_exec_and_private_data();
	check_expected_descriptor_rejects_stale_and_alien_mapping();
	check_restore_failure_keeps_mapped_private_page_owned();
	check_partial_pte_mutation_rolls_back_or_blocks_destroy();
	check_atomic_fault_register_snapshot_slot_emulation_and_commit();
	check_same_handle_reentry_fails_busy();
	check_active_maps_report_busy_until_destroy();
	puts("test-user-ptmap-contract: GREEN");
	return 0;
}

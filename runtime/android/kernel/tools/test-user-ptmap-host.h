/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_USER_PTMAP_HOST_H
#define NEVERC_KRT_TEST_USER_PTMAP_HOST_H

/* Test-only kernel boundary for nvk_user_ptmap.c host behavior tests. */

#include <stddef.h>
#include <stdint.h>

struct neverc_krt_user_ptmap_test_geometry {
	size_t page_size;
	unsigned int page_shift;
	unsigned int contiguous_entries;
	uint64_t descriptor_address_mask;
	uint64_t physical_address_mask;
	uint64_t physical_page_mask;
	uint64_t valid_mask;
	uint64_t user_mask;
	uint64_t readonly_mask;
	uint64_t write_mask;
	uint64_t uxn_mask;
	uint64_t pxn_mask;
	uint64_t ng_mask;
	uint64_t special_mask;
	uint64_t contiguous_mask;
	size_t pt_regs_size;
	size_t pt_regs_x;
	size_t pt_regs_x_size;
	size_t pt_regs_sp;
	size_t pt_regs_pc;
	size_t pt_regs_pstate;
	uint64_t pstate_mode_mask;
	uint64_t pstate_user_mode;
};

/* A locked host view of the complete contiguous-PTE group. */
struct neverc_krt_user_ptmap_test_pte_window {
	uint64_t *entries;
	size_t entry_count;
	size_t target_index;
	unsigned long group_address;
};

struct neverc_krt_user_ptmap_test_backend {
	const struct neverc_krt_user_ptmap_test_geometry *geometry;

	/* Sleepable install-only boundary. */
	int (*mmap_read_begin)(void *mm);
	void (*mmap_read_end)(void *mm);
	int (*mapping_validate)(void *mm, unsigned long address);
	/*
	 * install receives one owned mm_users lease.  At the success commit it
	 * first acquires mm_count, consumes mm_users, and the handle thereafter
	 * owns only mm_count.  Any install failure preserves the caller's lease.
	 */
	int (*mm_count_grab)(void *mm);
	void (*mm_users_put)(void *mm);
	void (*mm_count_drop)(void *mm);
	int (*matches_current_mm)(void *mm);

	/* Original pages are referenced; private pages are allocated/freed. */
	int (*original_page_get)(unsigned long expected_pfn,
				  unsigned long *actual_pfn,
				  void **page_address);
	void (*original_page_put)(unsigned long pfn, void *page_address);
	int (*private_page_alloc)(unsigned long *pfn, void **page_address);
	void (*private_page_free)(unsigned long pfn, void *page_address);

	/* Atomic-safe PTE, TLB, cache, and nofault register-memory boundary. */
	int (*rcu_read_begin)(void);
	void (*rcu_read_end)(void);
	int (*pte_lock)(void *mm, unsigned long address,
			struct neverc_krt_user_ptmap_test_pte_window *window);
	void (*pte_unlock)(void *mm,
			  struct neverc_krt_user_ptmap_test_pte_window *window);
	int (*pte_write)(uint64_t *entry, uint64_t value);
	void (*tlbi_range)(unsigned long address, unsigned int page_count);
	void (*sync_exec_page)(void *page_address, size_t size);
	int (*nofault_read)(void *destination, const void *source, size_t size);
	int (*nofault_write)(void *destination, const void *source, size_t size);
};

/* Defined by the future runtime only in NEVERC_KRT_USER_PTMAP_HOST_TEST mode. */
int neverc_krt_user_ptmap_test_bind_backend(
	const struct neverc_krt_user_ptmap_test_backend *backend);
int neverc_krt_user_ptmap_test_release(struct neverc_krt_user_ptmap **map);

enum neverc_krt_user_ptmap_test_pte_route {
	NEVERC_KRT_USER_PTMAP_TEST_FOLLOW_PTE = 1,
	NEVERC_KRT_USER_PTMAP_TEST_PTE_OFFSET_MAP_LOCK = 2,
};

enum neverc_krt_user_ptmap_test_pin_abi {
	NEVERC_KRT_USER_PTMAP_TEST_PIN_WITH_VMAS = 1,
	NEVERC_KRT_USER_PTMAP_TEST_PIN_WITHOUT_VMAS = 2,
};

enum neverc_krt_user_ptmap_test_alloc_abi {
	NEVERC_KRT_USER_PTMAP_TEST_GET_FREE_PAGES = 1,
	NEVERC_KRT_USER_PTMAP_TEST_GET_FREE_PAGES_NOPROF = 2,
};

struct neverc_krt_user_ptmap_test_profile_policy {
	uint64_t descriptor_address_mask;
	uint64_t physical_address_mask;
	uint64_t physical_page_mask;
	unsigned int pte_route;
	unsigned int pin_abi;
	unsigned int alloc_abi;
	unsigned int pte_release_internal_rcu;
};

/* Keep opaque fixture IDs on the test side; production dispatches only from
 * the generated profile's semantic Linux identity. */
static inline int neverc_krt_user_ptmap_test_profile_identity(
	unsigned int profile_id, unsigned int *linux_major,
	unsigned int *linux_minor)
{
	if (!linux_major || !linux_minor)
		return -1;
	switch (profile_id) {
	case 510:
	case 51013:
		*linux_major = 5;
		*linux_minor = 10;
		return 0;
	case 515:
	case 51514:
		*linux_major = 5;
		*linux_minor = 15;
		return 0;
	case 601:
		*linux_major = 6;
		*linux_minor = 1;
		return 0;
	case 606:
		*linux_major = 6;
		*linux_minor = 6;
		return 0;
	case 612:
		*linux_major = 6;
		*linux_minor = 12;
		return 0;
	case 618:
		*linux_major = 6;
		*linux_minor = 18;
		return 0;
	default:
		return -1;
	}
}

int neverc_krt_user_ptmap_test_profile_policy(
	unsigned int profile_id,
	struct neverc_krt_user_ptmap_test_profile_policy *policy);

enum neverc_krt_user_ptmap_test_kcfi_mode {
	NEVERC_KRT_USER_PTMAP_TEST_KCFI_DISABLED = 0,
	NEVERC_KRT_USER_PTMAP_TEST_KCFI_CLASSIC = 1,
	NEVERC_KRT_USER_PTMAP_TEST_KCFI_NORMALIZED = 2,
};

/*
 * Test the production architectural/KCFI availability gate.  Tag order is:
 * PTE acquire, pin, unpin, alloc, free, rwsem, spin unlock, RCU, find VMA,
 * access_remote_vm, mmput/__mmdrop, and the heap allocator.
 */
int neverc_krt_user_ptmap_test_runtime_gate(
	unsigned int profile_id, uint64_t tcr_el1, unsigned int kcfi_mode,
	const uint32_t *observed_tags, size_t observed_tag_count);

#endif /* NEVERC_KRT_TEST_USER_PTMAP_HOST_H */

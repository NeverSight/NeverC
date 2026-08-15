/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_TEST_USER_PTMAP_CONTRACT_API_H
#define NEVERC_KRT_TEST_USER_PTMAP_CONTRACT_API_H

/*
 * Frozen test-side sketch of the intended public <nvk_user_ptmap.h> API.
 *
 * This is deliberately not a production header.  The RED runner first uses
 * it to prove that the complete behavior fixture is well-formed, then builds
 * the same fixture against the real public header once that header exists.
 */

#include <stddef.h>
#include <stdint.h>

struct neverc_krt_user_ptmap;

/* Observable page-table roles.  They describe permissions as well as PFN. */
enum neverc_krt_user_ptmap_view {
	NEVERC_KRT_USER_PTMAP_ORIGINAL = 0,
	NEVERC_KRT_USER_PTMAP_EXEC_ONLY = 1,
	NEVERC_KRT_USER_PTMAP_USER_RW_NX = 2,
};

/* Backing slots used for private-page copies and fault emulation. */
enum neverc_krt_user_ptmap_slot {
	NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL = 0,
	NEVERC_KRT_USER_PTMAP_SLOT_EXEC = 1,
	NEVERC_KRT_USER_PTMAP_SLOT_USER_RW = 2,
};

#define NEVERC_KRT_USER_PTMAP_PRIVATE_EXEC    (1U << 0)
#define NEVERC_KRT_USER_PTMAP_PRIVATE_USER_RW (1U << 1)
#define NEVERC_KRT_USER_PTMAP_PRIVATE_ALL \
	(NEVERC_KRT_USER_PTMAP_PRIVATE_EXEC | \
	 NEVERC_KRT_USER_PTMAP_PRIVATE_USER_RW)

/*
 * @mm carries one owned mm_users lease into install.  On success install
 * acquires mm_count, consumes that mm_users lease, and the handle owns only
 * mm_count plus a reference to the original page.  Failure consumes nothing.
 * install is a sleepable control-path call: it may lock mmap, split a block,
 * and allocate every requested private slot.  It does not alter the PTE.
 * USER_RW_NX requires PRIVATE_USER_RW; EXEC_ONLY uses PRIVATE_EXEC when it was
 * requested, otherwise it is an execute-only view of the referenced original
 * PFN (an execute-only view of the original page).
 */
struct neverc_krt_user_ptmap_install {
	void *mm;
	unsigned long address;
	unsigned int private_slots;
};

/*
 * Scalar-only observation; no mm, PTE, PFN, ownership bit, page, or kernel VA
 * escapes.  Expected live descriptors/PFNs and all ownership stay in @map.
 */
struct neverc_krt_user_ptmap_status {
	unsigned long address;
	unsigned long page_size;
	enum neverc_krt_user_ptmap_view view;
	unsigned int private_slots;
	/* False for an externally replaced/unknown descriptor or permission set. */
	int current_matches;
	/* True whenever the live PTE still names any owned private PFN. */
	int private_mapped;
};

/* Value-only ARM64 user register snapshot.  Kernel pt_regs stays opaque. */
struct neverc_krt_user_fault_regs {
	uint64_t x[31];
	uint64_t sp;
	uint64_t pc;
	uint64_t pstate;
	int user_mode;
};

int neverc_krt_user_ptmap_available(void);
int neverc_krt_user_ptmap_busy(void);

int neverc_krt_user_ptmap_install(
	const struct neverc_krt_user_ptmap_install *request,
	struct neverc_krt_user_ptmap **out_map);

/*
 * Prepared-map operations below never allocate, resolve symbols, take
 * mmap_lock, or otherwise sleep; they are legal in the do_mem_abort path.
 * They serialize per handle and return -EBUSY instead of spinning on
 * same-handle concurrency or fault-path re-entry.  Destinations of copy/write
 * must be preallocated PRIVATE slots.  Bounds are checked before any bytes
 * are changed.  Executable-slot changes are made visible to instruction fetch
 * before the next EXEC_ONLY transition.
 */
int neverc_krt_user_ptmap_copy(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot destination,
	enum neverc_krt_user_ptmap_slot source);
int neverc_krt_user_ptmap_read(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot source,
	size_t offset, void *destination, size_t size);
int neverc_krt_user_ptmap_write(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot destination,
	size_t offset, const void *source, size_t size);
/* Publish pending private EXEC-slot writes without a PTE/view transition.
 * A clean map is a no-op. */
int neverc_krt_user_ptmap_sync_exec(
	struct neverc_krt_user_ptmap *map);

/*
 * Compare the complete live descriptor with the runtime-private descriptor
 * for @expected_view while holding the kernel PTE lock, then transition.  A
 * PFN change uses break-before-
 * make.  PTE_CONT is broken as one complete group with group TLBI before any
 * member is made.  A successful operation performs the final page TLBI.
 */
int neverc_krt_user_ptmap_transition(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view,
	enum neverc_krt_user_ptmap_view new_view);

/* Restore the saved original descriptor with the same compare semantics. */
int neverc_krt_user_ptmap_restore(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view);

/* Atomic-safe live query.  Unknown external mappings return current_matches=0. */
int neverc_krt_user_ptmap_query(
	struct neverc_krt_user_ptmap *map,
	struct neverc_krt_user_ptmap_status *status);

/* Opaque mm identity test for exit_mmap cleanup; no lease is transferred. */
int neverc_krt_user_ptmap_matches_mm(
	struct neverc_krt_user_ptmap *map, const void *borrowed_mm);
int neverc_krt_user_ptmap_matches_current_mm(
	struct neverc_krt_user_ptmap *map);

/*
 * destroy never restores a mapping.  It serializes per handle and returns
 * -EBUSY (leaving *map unchanged) if an operation is active or a locked query
 * cannot prove that no live valid PTE names an owned private page.  Only
 * success clears *map, releases the original-page/mm references, and frees
 * private pages.
 */
int neverc_krt_user_ptmap_destroy(struct neverc_krt_user_ptmap **map);

/* Atomic-safe opaque pt_regs value transfer. */
int neverc_krt_user_fault_regs_snapshot(
	const void *opaque_pt_regs,
	struct neverc_krt_user_fault_regs *snapshot);
int neverc_krt_user_fault_regs_commit(
	void *opaque_pt_regs,
	const struct neverc_krt_user_fault_regs *snapshot);

#endif /* NEVERC_KRT_TEST_USER_PTMAP_CONTRACT_API_H */

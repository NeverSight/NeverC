/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_USER_PTMAP_H
#define NEVERC_KRT_USER_PTMAP_H

#include <linux/types.h>

/*
 * A user mapping is deliberately represented by an incomplete type.  The
 * runtime owns every page-table descriptor, PFN, page reference, kernel
 * address and lock needed to operate on it.
 */
struct neverc_krt_user_ptmap;

enum neverc_krt_user_ptmap_view {
	NEVERC_KRT_USER_PTMAP_ORIGINAL = 0,
	NEVERC_KRT_USER_PTMAP_EXEC_ONLY = 1,
	NEVERC_KRT_USER_PTMAP_USER_RW_NX = 2,
};

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
 * install is a sleepable control-path operation.  request->mm must carry one
 * owned mm_users lease (for example from neverc_krt_task_mm_get).  At the
 * success commit the runtime first acquires mm_count, then consumes that
 * mm_users lease; the opaque handle thereafter owns only mm_count.  Failure
 * consumes nothing and leaves the input lease with the caller.  The handle
 * also owns a reference to the original page and every requested private
 * page.  It snapshots but does not alter the PTE.
 */
struct neverc_krt_user_ptmap_install {
	void *mm;
	unsigned long address;
	unsigned int private_slots;
};

/* Scalar-only status: no page-table or page identity crosses this boundary. */
struct neverc_krt_user_ptmap_status {
	unsigned long address;
	unsigned long page_size;
	enum neverc_krt_user_ptmap_view view;
	unsigned int private_slots;
	int current_matches;
	int private_mapped;
};

/* Value-only ARM64 register state; the kernel pt_regs object stays opaque. */
struct neverc_krt_user_fault_regs {
	u64 x[31];
	u64 sp;
	u64 pc;
	u64 pstate;
	int user_mode;
};

int neverc_krt_user_ptmap_available(void);

/*
 * True while any handle still owns pages or an mm_count, or while install
 * is still pinning/allocating.  Module unload must wait until this is
 * false; neverc_krt_cleanup_all fails closed with -EBUSY otherwise.
 */
int neverc_krt_user_ptmap_busy(void);

int neverc_krt_user_ptmap_install(
	const struct neverc_krt_user_ptmap_install *request,
	struct neverc_krt_user_ptmap **out_map);

/*
 * Prepared-map operations below are allocation-free and non-sleeping.  They
 * serialize per handle and return -EBUSY instead of spinning on same-handle
 * concurrency or fault-path re-entry.
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
/* Publish pending private EXEC-slot writes to instruction fetch.  A clean map
 * is a no-op; this operation does not inspect or change any PTE or view. */
int neverc_krt_user_ptmap_sync_exec(
	struct neverc_krt_user_ptmap *map);

int neverc_krt_user_ptmap_transition(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view,
	enum neverc_krt_user_ptmap_view new_view);
int neverc_krt_user_ptmap_restore(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view);
int neverc_krt_user_ptmap_query(
	struct neverc_krt_user_ptmap *map,
	struct neverc_krt_user_ptmap_status *status);

/*
 * Compare the handle with a borrowed opaque mm identity.  This exists for
 * exit_mmap cleanup and transfers no identity or lease to the caller.
 */
int neverc_krt_user_ptmap_matches_mm(
	struct neverc_krt_user_ptmap *map, const void *borrowed_mm);

/*
 * Non-sleeping current-task identity check for fault hooks.  This rejects PID
 * or TGID reuse where the scalar ID matches but the live mm does not.
 */
int neverc_krt_user_ptmap_matches_current_mm(
	struct neverc_krt_user_ptmap *map);

/*
 * destroy never restores a mapping.  The caller must first stop submitting
 * new operations through aliases of the handle.  It fails closed with -EBUSY
 * if an operation is active, the handle no longer knows the live PTE after a
 * partial mutation, or a locked query cannot prove that no live valid PTE
 * names an owned private page.
 */
int neverc_krt_user_ptmap_destroy(struct neverc_krt_user_ptmap **map);

int neverc_krt_user_fault_regs_snapshot(
	const void *opaque_pt_regs,
	struct neverc_krt_user_fault_regs *snapshot);
int neverc_krt_user_fault_regs_commit(
	void *opaque_pt_regs,
	const struct neverc_krt_user_fault_regs *snapshot);

#endif /* NEVERC_KRT_USER_PTMAP_H */

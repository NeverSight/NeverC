/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Opaque active user-page-table mapping core.
 *
 * All architecture and kernel ownership mechanics enter through the private
 * backend below.  The public surface contains only semantic views, byte-copy
 * operations and scalar status/register values.  Until a certified profile
 * backend is bound, the production runtime fails closed.
 */
#if defined(NEVERC_KRT_USER_PTMAP_HOST_TEST)
#include <nvk_user_ptmap.h>
#include "../tools/test-user-ptmap-host.h"

#include <errno.h>
#include <stdint.h>

/* The runtime's freestanding linux/types.h intentionally owns dev_t.  Avoid
 * pulling the host libc sys/types.h back through stdlib.h in this fixture. */
void *calloc(size_t count, size_t size);
void free(void *pointer);

#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOTSUP
#endif

typedef uint64_t _neverc_krt_ptmap_pte_t;

static void *_neverc_krt_ptmap_zalloc(size_t size)
{
	return calloc(1, size);
}

static void _neverc_krt_ptmap_free(const void *pointer)
{
	free((void *)pointer);
}
#else
#include <nvk_user_ptmap.h>
#include <nvk.h>
#include "nvk_internal.h"

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOTSUPP
#endif

typedef u64 _neverc_krt_ptmap_pte_t;

/* Exact private ABI wrappers.  These names and one-word representations match
 * arm64 GKI, but remain confined to the runtime backend. */
typedef struct {
	u64 pte;
} pte_t;
typedef struct {
	u64 pmd;
} pmd_t;

static void *_neverc_krt_ptmap_zalloc(size_t size);

static void _neverc_krt_ptmap_free(const void *pointer)
{
	kfree(pointer);
}
#endif

struct _neverc_krt_ptmap_geometry {
	size_t page_size;
	unsigned int page_shift;
	unsigned int contiguous_entries;
	_neverc_krt_ptmap_pte_t descriptor_address_mask;
	_neverc_krt_ptmap_pte_t physical_address_mask;
	_neverc_krt_ptmap_pte_t physical_page_mask;
	_neverc_krt_ptmap_pte_t valid_mask;
	_neverc_krt_ptmap_pte_t user_mask;
	_neverc_krt_ptmap_pte_t readonly_mask;
	_neverc_krt_ptmap_pte_t write_mask;
	_neverc_krt_ptmap_pte_t uxn_mask;
	_neverc_krt_ptmap_pte_t pxn_mask;
	_neverc_krt_ptmap_pte_t ng_mask;
	_neverc_krt_ptmap_pte_t special_mask;
	_neverc_krt_ptmap_pte_t contiguous_mask;
	size_t pt_regs_size;
	size_t pt_regs_x;
	size_t pt_regs_sp;
	size_t pt_regs_pc;
	size_t pt_regs_pstate;
	_neverc_krt_ptmap_pte_t pstate_mode_mask;
	_neverc_krt_ptmap_pte_t pstate_user_mode;
};
typedef struct _neverc_krt_ptmap_geometry _neverc_krt_ptmap_geometry_t;

enum _neverc_krt_ptmap_pte_route {
	_NEVERC_KRT_PTMAP_FOLLOW_PTE = 1,
	_NEVERC_KRT_PTMAP_PTE_OFFSET_MAP_LOCK = 2,
};

enum _neverc_krt_ptmap_pin_abi {
	_NEVERC_KRT_PTMAP_PIN_WITH_VMAS = 1,
	_NEVERC_KRT_PTMAP_PIN_WITHOUT_VMAS = 2,
};

enum _neverc_krt_ptmap_alloc_abi {
	_NEVERC_KRT_PTMAP_GET_FREE_PAGES = 1,
	_NEVERC_KRT_PTMAP_GET_FREE_PAGES_NOPROF = 2,
};

enum _neverc_krt_ptmap_kcfi_mode {
	_NEVERC_KRT_PTMAP_KCFI_DISABLED = 0,
	_NEVERC_KRT_PTMAP_KCFI_CLASSIC = 1,
	_NEVERC_KRT_PTMAP_KCFI_NORMALIZED = 2,
};

struct _neverc_krt_ptmap_profile_policy {
	_neverc_krt_ptmap_pte_t descriptor_address_mask;
	_neverc_krt_ptmap_pte_t physical_address_mask;
	_neverc_krt_ptmap_pte_t physical_page_mask;
	unsigned int pte_route;
	unsigned int pin_abi;
	unsigned int alloc_abi;
	unsigned int pte_release_internal_rcu;
	unsigned int use_find_vma_prev;
	unsigned int kcfi_mode;
	u32 kcfi_pte_acquire;
	u32 kcfi_pin;
	u32 kcfi_unpin;
	u32 kcfi_alloc;
	u32 kcfi_free;
	u32 kcfi_rwsem;
	u32 kcfi_spin_unlock;
	u32 kcfi_rcu;
	u32 kcfi_find_vma;
	u32 kcfi_access_remote;
	u32 kcfi_mmput;
	u32 kcfi_heap_alloc;
};

#define _NEVERC_KRT_PTMAP_PA48_ADDRESS_MASK 0x0000ffffffffffffULL
#define _NEVERC_KRT_PTMAP_PA48_PAGE_MASK 0x0000fffffffff000ULL
#define _NEVERC_KRT_PTMAP_DESC50_MASK 0x0003fffffffff000ULL

/*
 * Profile IDs are opaque handles owned by the generated policy table.  Build
 * the private helper ABI from that table's semantic Linux identity instead of
 * re-enumerating opaque IDs here.  Each accepted major/minor pair is exact;
 * an unproved future series remains unavailable until its ABI is certified.
 */
static int _neverc_krt_ptmap_policy_for_kernel(
	unsigned int linux_major, unsigned int linux_minor,
	struct _neverc_krt_ptmap_profile_policy *policy)
{
	if (!policy)
		return -EINVAL;
	__builtin_memset(policy, 0, sizeof(*policy));
	policy->physical_address_mask = _NEVERC_KRT_PTMAP_PA48_ADDRESS_MASK;
	policy->physical_page_mask = _NEVERC_KRT_PTMAP_PA48_PAGE_MASK;

	if (linux_major == 5U &&
	    (linux_minor == 10U || linux_minor == 15U)) {
		policy->descriptor_address_mask =
			_NEVERC_KRT_PTMAP_PA48_PAGE_MASK;
		policy->pte_route = _NEVERC_KRT_PTMAP_FOLLOW_PTE;
		policy->pin_abi = _NEVERC_KRT_PTMAP_PIN_WITH_VMAS;
		policy->alloc_abi = _NEVERC_KRT_PTMAP_GET_FREE_PAGES;
		policy->use_find_vma_prev = linux_minor == 15U;
		policy->kcfi_mode = _NEVERC_KRT_PTMAP_KCFI_DISABLED;
		return 0;
	}
	if (linux_major == 6U && linux_minor == 1U) {
		policy->descriptor_address_mask =
			_NEVERC_KRT_PTMAP_PA48_PAGE_MASK;
		policy->pte_route = _NEVERC_KRT_PTMAP_FOLLOW_PTE;
		policy->pin_abi = _NEVERC_KRT_PTMAP_PIN_WITH_VMAS;
		policy->alloc_abi = _NEVERC_KRT_PTMAP_GET_FREE_PAGES;
		policy->kcfi_mode = _NEVERC_KRT_PTMAP_KCFI_CLASSIC;
		policy->kcfi_pte_acquire = 0x14f277e5U;
		policy->kcfi_pin = 0x485e012fU;
	} else if (linux_major == 6U && linux_minor == 6U) {
		policy->descriptor_address_mask =
			_NEVERC_KRT_PTMAP_PA48_PAGE_MASK;
		policy->pte_route = _NEVERC_KRT_PTMAP_FOLLOW_PTE;
		policy->pin_abi = _NEVERC_KRT_PTMAP_PIN_WITHOUT_VMAS;
		policy->alloc_abi = _NEVERC_KRT_PTMAP_GET_FREE_PAGES;
		policy->pte_release_internal_rcu = 1;
		policy->kcfi_mode = _NEVERC_KRT_PTMAP_KCFI_CLASSIC;
		policy->kcfi_pte_acquire = 0x14f277e5U;
		policy->kcfi_pin = 0x208b8d20U;
	} else if (linux_major == 6U &&
		   (linux_minor == 12U || linux_minor == 18U)) {
		policy->descriptor_address_mask = _NEVERC_KRT_PTMAP_DESC50_MASK;
		policy->pte_route = _NEVERC_KRT_PTMAP_PTE_OFFSET_MAP_LOCK;
		policy->pin_abi = _NEVERC_KRT_PTMAP_PIN_WITHOUT_VMAS;
		policy->alloc_abi = _NEVERC_KRT_PTMAP_GET_FREE_PAGES_NOPROF;
		policy->pte_release_internal_rcu = 1;
		policy->kcfi_mode = _NEVERC_KRT_PTMAP_KCFI_NORMALIZED;
		policy->kcfi_pte_acquire = 0x53c2bd3fU;
		policy->kcfi_pin = 0x62f58ccbU;
		policy->kcfi_unpin = 0x35183cccU;
		policy->kcfi_alloc = 0x3c5d8714U;
		policy->kcfi_free = 0xc6ba6ed7U;
		policy->kcfi_rwsem = 0x7d66fa54U;
		policy->kcfi_spin_unlock = 0x9f823190U;
		policy->kcfi_rcu = 0xe5c47d60U;
		policy->kcfi_find_vma = 0x9fa57c4cU;
		policy->kcfi_access_remote = 0xcef73528U;
		policy->kcfi_mmput = 0xa65f2b02U;
		policy->kcfi_heap_alloc = 0xb57726fcU;
		return 0;
	} else {
		__builtin_memset(policy, 0, sizeof(*policy));
		return -EOPNOTSUPP;
	}

	policy->kcfi_unpin = 0xfee925afU;
	policy->kcfi_alloc = 0x3086ada5U;
	policy->kcfi_free = 0xf2d356caU;
	policy->kcfi_rwsem = 0x31f85444U;
	policy->kcfi_spin_unlock = 0xaa91dafaU;
	policy->kcfi_rcu = 0xa540670cU;
	policy->kcfi_find_vma = 0x2b60aac7U;
	policy->kcfi_access_remote = 0x51aeb208U;
	policy->kcfi_mmput = 0x8e0b794cU;
	policy->kcfi_heap_alloc = 0x1d7a58e3U;
	return 0;
}

#define _NEVERC_KRT_PTMAP_KCFI_TAG_COUNT 12U

static int _neverc_krt_ptmap_runtime_gate(
	const struct _neverc_krt_ptmap_profile_policy *policy,
	u64 tcr_el1, unsigned int kcfi_mode, const u32 *observed_tags,
	size_t observed_tag_count)
{
	u32 expected_tags[_NEVERC_KRT_PTMAP_KCFI_TAG_COUNT];
	size_t i;

	if (!policy || ((tcr_el1 >> 14) & 3ULL) != 0ULL ||
	    (tcr_el1 & 0x3fULL) != 25ULL ||
	    /* IPS (TCR_EL1.IPS) selects the intermediate PA size.  Accept any
	     * size that fits the 48-bit physical_address_mask (IPS 0..5 → ≤48-bit
	     * PA); reject 52-bit (IPS 6/7) which needs the LPA2 OA[51:50] low-bit
	     * encoding this backend does not implement.  A device with a smaller
	     * PA than the profile (e.g. IPS=4, 44-bit) is safe: its PFNs fit and
	     * the unused high OA-field bits are RES0/zero. */
	    ((tcr_el1 >> 32) & 7ULL) > 5ULL ||
	    kcfi_mode != policy->kcfi_mode)
		return -EOPNOTSUPP;
	if (kcfi_mode == _NEVERC_KRT_PTMAP_KCFI_DISABLED)
		return observed_tag_count == 0 ? 0 : -EOPNOTSUPP;
	if (!observed_tags ||
	    observed_tag_count != _NEVERC_KRT_PTMAP_KCFI_TAG_COUNT)
		return -EOPNOTSUPP;

	expected_tags[0] = policy->kcfi_pte_acquire;
	expected_tags[1] = policy->kcfi_pin;
	expected_tags[2] = policy->kcfi_unpin;
	expected_tags[3] = policy->kcfi_alloc;
	expected_tags[4] = policy->kcfi_free;
	expected_tags[5] = policy->kcfi_rwsem;
	expected_tags[6] = policy->kcfi_spin_unlock;
	expected_tags[7] = policy->kcfi_rcu;
	expected_tags[8] = policy->kcfi_find_vma;
	expected_tags[9] = policy->kcfi_access_remote;
	expected_tags[10] = policy->kcfi_mmput;
	expected_tags[11] = policy->kcfi_heap_alloc;
	for (i = 0; i < _NEVERC_KRT_PTMAP_KCFI_TAG_COUNT; i++) {
		if (!expected_tags[i] || observed_tags[i] != expected_tags[i])
			return -EOPNOTSUPP;
	}
	return 0;
}

struct _neverc_krt_ptmap_pte_window {
	_neverc_krt_ptmap_pte_t *entries;
	size_t entry_count;
	size_t target_index;
	unsigned long group_address;
	void *lock_token;
	unsigned int backend_flags;
};
typedef struct _neverc_krt_ptmap_pte_window
	_neverc_krt_ptmap_pte_window_t;

#define _NEVERC_KRT_PTMAP_WINDOW_INTERNAL_RCU (1U << 0)
#define _NEVERC_KRT_PTMAP_WINDOW_HELD_MMAP (1U << 1)
#define _NEVERC_KRT_PTMAP_WINDOW_MMAP_PREHELD (1U << 2)

/*
 * The original-page boundary is address/mm based.  expected_pfn is only a
 * private identity assertion for the implementation after it pins by user
 * address; it is never a request to synthesize a struct page from a PFN.
 * A real profile backend can therefore use pin_user_pages_remote, retain its
 * opaque pin token, and revalidate the locked PTE around this operation.
 */
struct _neverc_krt_ptmap_backend {
	const _neverc_krt_ptmap_geometry_t *geometry;
	int (*mmap_read_begin)(void *mm);
	void (*mmap_read_end)(void *mm);
	int (*mapping_validate)(void *mm, unsigned long address);
	int (*mm_count_grab)(void *mm);
	void (*mm_users_put)(void *mm);
	void (*mm_count_drop)(void *mm);
	int (*matches_current_mm)(void *mm);
	int (*original_page_pin)(void *mm, unsigned long address,
				 unsigned long expected_pfn,
				 _neverc_krt_ptmap_pte_t expected_descriptor,
				 void **pin_token);
	int (*original_page_snapshot)(void *mm, unsigned long address,
				      void *pin_token, void **page_address);
	void (*original_page_unpin)(void *mm, unsigned long address,
				    unsigned long expected_pfn, void *pin_token,
				    void *page_address);
	int (*private_page_alloc)(unsigned long *pfn, void **page_address);
	void (*private_page_free)(unsigned long pfn, void *page_address);
	int (*rcu_read_begin)(void);
	void (*rcu_read_end)(void);
	int (*pte_lock)(void *mm, unsigned long address,
			_neverc_krt_ptmap_pte_window_t *window);
	void (*pte_unlock)(void *mm,
			   _neverc_krt_ptmap_pte_window_t *window);
	int (*pte_write)(_neverc_krt_ptmap_pte_t *entry,
			 _neverc_krt_ptmap_pte_t value);
	void (*tlbi_range)(unsigned long address, unsigned int page_count);
	void (*sync_exec_page)(void *page_address, size_t size);
	int (*nofault_read)(void *destination, const void *source, size_t size);
	int (*nofault_write)(void *destination, const void *source, size_t size);
};
typedef struct _neverc_krt_ptmap_backend _neverc_krt_ptmap_backend_t;

#define _NEVERC_KRT_PTMAP_MAGIC 0x4e5650544d415031ULL

struct _neverc_krt_ptmap_backing {
	unsigned long pfn;
	void *address;
	void *pin_token;
	int referenced;
	int owned;
};

struct neverc_krt_user_ptmap {
	u64 magic;
	const _neverc_krt_ptmap_backend_t *backend;
	void *mm;
	int mm_count_owned;
	int mmap_held;
	unsigned long address;
	unsigned int private_slots;
	enum neverc_krt_user_ptmap_view view;
	volatile int operation_lock;
	int state_known;
	int exec_needs_sync;
	_neverc_krt_ptmap_pte_t original_raw;
	_neverc_krt_ptmap_pte_t original_normalized;
	_neverc_krt_ptmap_pte_t current_descriptor;
	struct _neverc_krt_ptmap_backing original;
	struct _neverc_krt_ptmap_backing exec;
	struct _neverc_krt_ptmap_backing user_rw;
	_neverc_krt_ptmap_pte_t *contiguous_group;
	size_t contiguous_count;
	size_t contiguous_target;
	unsigned long contiguous_address;
	int contiguous_live;
};

struct _neverc_krt_ptmap_locked {
	_neverc_krt_ptmap_pte_window_t window;
	int rcu_locked;
	int pte_locked;
};

static const _neverc_krt_ptmap_backend_t *_neverc_krt_ptmap_backend;
static unsigned int _neverc_krt_ptmap_active_maps;
static unsigned int _neverc_krt_ptmap_pending_installs;
static unsigned int _neverc_krt_ptmap_cleanup_blocked;

#if defined(NEVERC_KRT_USER_PTMAP_HOST_TEST)
static const struct neverc_krt_user_ptmap_test_backend
	*_neverc_krt_ptmap_host_backend;
static _neverc_krt_ptmap_geometry_t _neverc_krt_ptmap_host_geometry;
static _neverc_krt_ptmap_backend_t _neverc_krt_ptmap_host_adapter;

int neverc_krt_user_ptmap_test_profile_policy(
	unsigned int profile_id,
	struct neverc_krt_user_ptmap_test_profile_policy *output)
{
	struct _neverc_krt_ptmap_profile_policy policy;
	unsigned int linux_major;
	unsigned int linux_minor;

	if (!output)
		return -EINVAL;
	__builtin_memset(output, 0, sizeof(*output));
	if (neverc_krt_user_ptmap_test_profile_identity(
			profile_id, &linux_major, &linux_minor) ||
	    _neverc_krt_ptmap_policy_for_kernel(
			linux_major, linux_minor, &policy))
		return -EOPNOTSUPP;
	output->descriptor_address_mask = policy.descriptor_address_mask;
	output->physical_address_mask = policy.physical_address_mask;
	output->physical_page_mask = policy.physical_page_mask;
	output->pte_route = policy.pte_route;
	output->pin_abi = policy.pin_abi;
	output->alloc_abi = policy.alloc_abi;
	output->pte_release_internal_rcu =
		policy.pte_release_internal_rcu;
	return 0;
}

int neverc_krt_user_ptmap_test_runtime_gate(
	unsigned int profile_id, uint64_t tcr_el1, unsigned int kcfi_mode,
	const uint32_t *observed_tags, size_t observed_tag_count)
{
	struct _neverc_krt_ptmap_profile_policy policy;
	unsigned int linux_major;
	unsigned int linux_minor;

	if (neverc_krt_user_ptmap_test_profile_identity(
			profile_id, &linux_major, &linux_minor) ||
	    _neverc_krt_ptmap_policy_for_kernel(
			linux_major, linux_minor, &policy))
		return -EOPNOTSUPP;
	return _neverc_krt_ptmap_runtime_gate(
		&policy, tcr_el1, kcfi_mode, observed_tags, observed_tag_count);
}

static int _neverc_krt_ptmap_host_original_pin(
	void *mm, unsigned long address, unsigned long expected_pfn,
	_neverc_krt_ptmap_pte_t expected_descriptor, void **pin_token)
{
	unsigned long actual_pfn = 0;
	void *page_address = (void *)0;
	int result;

	(void)mm;
	(void)address;
	(void)expected_descriptor;
	if (!pin_token)
		return -EINVAL;
	*pin_token = (void *)0;
	result = _neverc_krt_ptmap_host_backend->original_page_get(
		expected_pfn, &actual_pfn, &page_address);
	if (result)
		return result;
	if (actual_pfn != expected_pfn) {
		_neverc_krt_ptmap_host_backend->original_page_put(
			actual_pfn, page_address);
		return -EAGAIN;
	}
	*pin_token = page_address;
	return 0;
}

static int _neverc_krt_ptmap_host_original_snapshot(
	void *mm, unsigned long address, void *pin_token, void **page_address)
{
	(void)mm;
	(void)address;
	if (!pin_token || !page_address)
		return -EINVAL;
	*page_address = pin_token;
	return 0;
}

static void _neverc_krt_ptmap_host_original_unpin(
	void *mm, unsigned long address, unsigned long expected_pfn,
	void *pin_token, void *page_address)
{
	(void)mm;
	(void)address;
	(void)pin_token;
	_neverc_krt_ptmap_host_backend->original_page_put(
		expected_pfn, page_address);
}

static int _neverc_krt_ptmap_host_pte_lock(
	void *mm, unsigned long address,
	_neverc_krt_ptmap_pte_window_t *window)
{
	struct neverc_krt_user_ptmap_test_pte_window host_window;
	int result;

	__builtin_memset(&host_window, 0, sizeof(host_window));
	result = _neverc_krt_ptmap_host_backend->pte_lock(
		mm, address, &host_window);
	if (result)
		return result;
	window->entries = host_window.entries;
	window->entry_count = host_window.entry_count;
	window->target_index = host_window.target_index;
	window->group_address = host_window.group_address;
	return 0;
}

static void _neverc_krt_ptmap_host_pte_unlock(
	void *mm, _neverc_krt_ptmap_pte_window_t *window)
{
	struct neverc_krt_user_ptmap_test_pte_window host_window = {
		.entries = window->entries,
		.entry_count = window->entry_count,
		.target_index = window->target_index,
		.group_address = window->group_address,
	};

	_neverc_krt_ptmap_host_backend->pte_unlock(mm, &host_window);
}
#endif

static int _neverc_krt_ptmap_size_fits(size_t object_size, size_t offset,
				       size_t width)
{
	return width && offset <= object_size && width <= object_size - offset;
}

static int _neverc_krt_ptmap_ranges_overlap(size_t left, size_t left_size,
					    size_t right, size_t right_size)
{
	return !(left + left_size <= right || right + right_size <= left);
}

static int _neverc_krt_ptmap_geometry_valid(
	const _neverc_krt_ptmap_geometry_t *geometry)
{
	const size_t scalar_size = sizeof(u64);
	const size_t x_size = sizeof(u64) * 31U;
	_neverc_krt_ptmap_pte_t attribute_masks[9];
	size_t i;
	size_t j;

	if (!geometry || !geometry->page_size ||
	    geometry->page_shift >= sizeof(size_t) * 8U ||
	    geometry->page_size != ((size_t)1U << geometry->page_shift) ||
	    !geometry->contiguous_entries ||
	    geometry->contiguous_entries > 4096U ||
	    !geometry->descriptor_address_mask ||
	    !geometry->physical_address_mask ||
	    !geometry->physical_page_mask ||
	    !geometry->valid_mask || !geometry->user_mask ||
	    !geometry->readonly_mask || !geometry->write_mask ||
	    !geometry->uxn_mask || !geometry->pxn_mask ||
	    !geometry->ng_mask || !geometry->special_mask ||
	    !geometry->contiguous_mask ||
	    (geometry->descriptor_address_mask & (geometry->page_size - 1U)) ||
	    (geometry->physical_page_mask & (geometry->page_size - 1U)) ||
	    geometry->physical_address_mask !=
		(geometry->physical_page_mask | (geometry->page_size - 1U)) ||
	    (geometry->physical_page_mask & ~geometry->physical_address_mask) ||
	    (geometry->physical_page_mask &
	     ~geometry->descriptor_address_mask) ||
	    !geometry->pstate_mode_mask)
		return 0;

	attribute_masks[0] = geometry->valid_mask;
	attribute_masks[1] = geometry->user_mask;
	attribute_masks[2] = geometry->readonly_mask;
	attribute_masks[3] = geometry->write_mask;
	attribute_masks[4] = geometry->uxn_mask;
	attribute_masks[5] = geometry->pxn_mask;
	attribute_masks[6] = geometry->ng_mask;
	attribute_masks[7] = geometry->special_mask;
	attribute_masks[8] = geometry->contiguous_mask;
	for (i = 0; i < sizeof(attribute_masks) / sizeof(attribute_masks[0]);
	     i++) {
		if (attribute_masks[i] & geometry->descriptor_address_mask)
			return 0;
		for (j = i + 1U;
		     j < sizeof(attribute_masks) / sizeof(attribute_masks[0]);
		     j++) {
			if (attribute_masks[i] & attribute_masks[j])
				return 0;
		}
	}

	if (!_neverc_krt_ptmap_size_fits(geometry->pt_regs_size,
					 geometry->pt_regs_x, x_size) ||
	    !_neverc_krt_ptmap_size_fits(geometry->pt_regs_size,
					 geometry->pt_regs_sp, scalar_size) ||
	    !_neverc_krt_ptmap_size_fits(geometry->pt_regs_size,
					 geometry->pt_regs_pc, scalar_size) ||
	    !_neverc_krt_ptmap_size_fits(geometry->pt_regs_size,
					 geometry->pt_regs_pstate, scalar_size) ||
	    _neverc_krt_ptmap_ranges_overlap(geometry->pt_regs_x, x_size,
					      geometry->pt_regs_sp, scalar_size) ||
	    _neverc_krt_ptmap_ranges_overlap(geometry->pt_regs_x, x_size,
					      geometry->pt_regs_pc, scalar_size) ||
	    _neverc_krt_ptmap_ranges_overlap(geometry->pt_regs_x, x_size,
					      geometry->pt_regs_pstate,
					      scalar_size) ||
	    _neverc_krt_ptmap_ranges_overlap(geometry->pt_regs_sp, scalar_size,
					      geometry->pt_regs_pc, scalar_size) ||
	    _neverc_krt_ptmap_ranges_overlap(geometry->pt_regs_sp, scalar_size,
					      geometry->pt_regs_pstate,
					      scalar_size) ||
	    _neverc_krt_ptmap_ranges_overlap(geometry->pt_regs_pc, scalar_size,
					      geometry->pt_regs_pstate,
					      scalar_size))
		return 0;

	return 1;
}

static int _neverc_krt_ptmap_backend_valid(
	const _neverc_krt_ptmap_backend_t *backend)
{
	return backend && _neverc_krt_ptmap_geometry_valid(backend->geometry) &&
		backend->mmap_read_begin && backend->mmap_read_end &&
		backend->mapping_validate &&
		backend->mm_count_grab && backend->mm_users_put &&
		backend->mm_count_drop && backend->matches_current_mm &&
		backend->original_page_pin && backend->original_page_snapshot &&
		backend->original_page_unpin &&
		backend->private_page_alloc && backend->private_page_free &&
		backend->rcu_read_begin && backend->rcu_read_end &&
		backend->pte_lock && backend->pte_unlock && backend->pte_write &&
		backend->tlbi_range && backend->sync_exec_page &&
		backend->nofault_read && backend->nofault_write;
}

static int _neverc_krt_ptmap_valid(const struct neverc_krt_user_ptmap *map)
{
	return map && map->magic == _NEVERC_KRT_PTMAP_MAGIC && map->backend &&
		_neverc_krt_ptmap_backend_valid(map->backend);
}

static int _neverc_krt_ptmap_operation_begin(
	struct neverc_krt_user_ptmap *map)
{
	if (!_neverc_krt_ptmap_valid(map))
		return -EINVAL;
	if (__atomic_exchange_n(&map->operation_lock, 1, __ATOMIC_ACQUIRE))
		return -EBUSY;
	if (!_neverc_krt_ptmap_valid(map)) {
		__atomic_store_n(&map->operation_lock, 0, __ATOMIC_RELEASE);
		return -EINVAL;
	}
	return 0;
}

static void _neverc_krt_ptmap_operation_end(
	struct neverc_krt_user_ptmap *map)
{
	__atomic_store_n(&map->operation_lock, 0, __ATOMIC_RELEASE);
}

static int _neverc_krt_ptmap_view_valid(
	enum neverc_krt_user_ptmap_view view)
{
	return view == NEVERC_KRT_USER_PTMAP_ORIGINAL ||
		view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY ||
		view == NEVERC_KRT_USER_PTMAP_USER_RW_NX;
}

static int _neverc_krt_ptmap_window_valid(
	const struct neverc_krt_user_ptmap *map,
	const _neverc_krt_ptmap_pte_window_t *window)
{
	const _neverc_krt_ptmap_geometry_t *geometry = map->backend->geometry;
	unsigned long target_offset;

	if (!window->entries || !window->entry_count ||
	    window->target_index >= window->entry_count ||
	    window->target_index > (~0UL / geometry->page_size))
		return 0;
	target_offset = (unsigned long)window->target_index *
			geometry->page_size;
	if (window->group_address > ~0UL - target_offset)
		return 0;
	return window->group_address + target_offset == map->address;
}

static void _neverc_krt_ptmap_unlock(
	struct neverc_krt_user_ptmap *map,
	struct _neverc_krt_ptmap_locked *locked)
{
	if (locked->pte_locked) {
		map->backend->pte_unlock(map->mm, &locked->window);
		locked->pte_locked = 0;
	}
	if (locked->rcu_locked) {
		map->backend->rcu_read_end();
		locked->rcu_locked = 0;
	}
}

static int _neverc_krt_ptmap_lock(struct neverc_krt_user_ptmap *map,
				  struct _neverc_krt_ptmap_locked *locked)
{
	int result;

	__builtin_memset(locked, 0, sizeof(*locked));
	if (map->mmap_held)
		locked->window.backend_flags =
			_NEVERC_KRT_PTMAP_WINDOW_MMAP_PREHELD;
	result = map->backend->rcu_read_begin();
	if (result)
		return result;
	locked->rcu_locked = 1;
	result = map->backend->pte_lock(map->mm, map->address,
					&locked->window);
	if (result) {
		_neverc_krt_ptmap_unlock(map, locked);
		return result;
	}
	locked->pte_locked = 1;
	if (!_neverc_krt_ptmap_window_valid(map, &locked->window)) {
		_neverc_krt_ptmap_unlock(map, locked);
		return -EFAULT;
	}
	return 0;
}

static int _neverc_krt_ptmap_pfn_encodable(
	const _neverc_krt_ptmap_geometry_t *geometry, unsigned long pfn)
{
	_neverc_krt_ptmap_pte_t encoded;
	_neverc_krt_ptmap_pte_t maximum_pfn;

	if (geometry->page_shift >= sizeof(_neverc_krt_ptmap_pte_t) * 8U)
		return 0;
	maximum_pfn = geometry->physical_page_mask >> geometry->page_shift;
	if ((_neverc_krt_ptmap_pte_t)pfn > maximum_pfn)
		return 0;
	encoded = (_neverc_krt_ptmap_pte_t)pfn << geometry->page_shift;
	return (encoded & geometry->physical_page_mask) == encoded &&
		(encoded & geometry->descriptor_address_mask) == encoded;
}

static unsigned long _neverc_krt_ptmap_descriptor_pfn(
	const _neverc_krt_ptmap_geometry_t *geometry,
	_neverc_krt_ptmap_pte_t descriptor)
{
	return (unsigned long)((descriptor & geometry->descriptor_address_mask) >>
			       geometry->page_shift);
}

static _neverc_krt_ptmap_pte_t _neverc_krt_ptmap_descriptor_for(
	const struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view view)
{
	const _neverc_krt_ptmap_geometry_t *geometry = map->backend->geometry;
	_neverc_krt_ptmap_pte_t descriptor = map->original_normalized;
	_neverc_krt_ptmap_pte_t mutable_masks;
	unsigned long pfn;

	if (view == NEVERC_KRT_USER_PTMAP_ORIGINAL)
		return map->original_normalized;
	pfn = view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY ? map->exec.pfn :
		map->user_rw.pfn;
	mutable_masks = geometry->user_mask | geometry->readonly_mask |
		geometry->write_mask | geometry->uxn_mask |
		geometry->pxn_mask | geometry->ng_mask |
		geometry->special_mask | geometry->contiguous_mask;
	descriptor &= ~(geometry->descriptor_address_mask | mutable_masks);
	descriptor |= ((_neverc_krt_ptmap_pte_t)pfn << geometry->page_shift) &
		geometry->descriptor_address_mask;
	descriptor |= geometry->valid_mask | geometry->pxn_mask |
		geometry->ng_mask | geometry->special_mask;
	if (view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY)
		/*
		 * EL0 execute-only (XOM): clear PTE_USER (AP[1]=0) so any EL0
		 * data read/write faults, while PTE_UXN stays clear (left
		 * cleared above) so EL0 may still execute.  PTE_RDONLY +
		 * PTE_PXN complete the read-only, EL1-noexec view.  Setting
		 * PTE_USER here would make the page EL0-readable and defeat
		 * execute-only.
		 */
		descriptor |= geometry->readonly_mask;
	else
		descriptor |= geometry->user_mask | geometry->write_mask |
			geometry->uxn_mask;
	return descriptor;
}

static struct _neverc_krt_ptmap_backing *_neverc_krt_ptmap_slot(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot slot)
{
	switch (slot) {
	case NEVERC_KRT_USER_PTMAP_SLOT_ORIGINAL:
		return &map->original;
	case NEVERC_KRT_USER_PTMAP_SLOT_EXEC:
		return &map->exec;
	case NEVERC_KRT_USER_PTMAP_SLOT_USER_RW:
		return &map->user_rw;
	default:
		return (void *)0;
	}
}

static int _neverc_krt_ptmap_private_pfn_mapped(
	const struct neverc_krt_user_ptmap *map,
	_neverc_krt_ptmap_pte_t descriptor)
{
	const _neverc_krt_ptmap_geometry_t *geometry = map->backend->geometry;
	unsigned long pfn;

	if (!(descriptor & geometry->valid_mask))
		return 0;
	pfn = _neverc_krt_ptmap_descriptor_pfn(geometry, descriptor);
	return (map->exec.owned && pfn == map->exec.pfn) ||
		(map->user_rw.owned && pfn == map->user_rw.pfn);
}

static void _neverc_krt_ptmap_release(struct neverc_krt_user_ptmap *map)
{
	const _neverc_krt_ptmap_backend_t *backend = map->backend;

	map->magic = 0;
	if (map->user_rw.owned)
		backend->private_page_free(map->user_rw.pfn,
					   map->user_rw.address);
	if (map->exec.owned)
		backend->private_page_free(map->exec.pfn, map->exec.address);
	if (map->original.referenced)
		backend->original_page_unpin(
			map->mm, map->address, map->original.pfn,
			map->original.pin_token, map->original.address);
	if (map->mm_count_owned)
		backend->mm_count_drop(map->mm);
	_neverc_krt_ptmap_free(map->contiguous_group);
	_neverc_krt_ptmap_free(map);
	__atomic_fetch_sub(&_neverc_krt_ptmap_active_maps, 1U,
			   __ATOMIC_RELEASE);
}

#if !defined(NEVERC_KRT_USER_PTMAP_HOST_TEST)

struct spinlock;
struct raw_spinlock;

typedef int (*_neverc_krt_ptmap_follow_pte_fn)(
	struct mm_struct *mm, unsigned long address, pte_t **ptep,
	struct spinlock **ptl);
typedef pte_t *(*_neverc_krt_ptmap_pte_offset_map_lock_fn)(
	struct mm_struct *mm, pmd_t *pmd, unsigned long address,
	struct spinlock **ptl);
typedef long (*_neverc_krt_ptmap_pin_with_vmas_fn)(
	struct mm_struct *mm, unsigned long start, unsigned long nr_pages,
	unsigned int gup_flags, struct page **pages,
	struct vm_area_struct **vmas, int *locked);
typedef long (*_neverc_krt_ptmap_pin_without_vmas_fn)(
	struct mm_struct *mm, unsigned long start, unsigned long nr_pages,
	unsigned int gup_flags, struct page **pages, int *locked);
typedef void (*_neverc_krt_ptmap_unpin_fn)(struct page *page);
typedef unsigned long (*_neverc_krt_ptmap_alloc_pages_fn)(
	gfp_t gfp_mask, unsigned int order);
typedef void (*_neverc_krt_ptmap_free_pages_fn)(
	unsigned long address, unsigned int order);
typedef void (*_neverc_krt_ptmap_rwsem_fn)(struct rw_semaphore *sem);
typedef int (*_neverc_krt_ptmap_rwsem_trylock_fn)(struct rw_semaphore *sem);
typedef void (*_neverc_krt_ptmap_spin_unlock_fn)(
	struct raw_spinlock *lock);
typedef void (*_neverc_krt_ptmap_rcu_fn)(void);
typedef struct vm_area_struct *(*_neverc_krt_ptmap_find_vma_fn)(
	struct mm_struct *mm, unsigned long address);
typedef struct vm_area_struct *(*_neverc_krt_ptmap_find_vma_prev_fn)(
	struct mm_struct *mm, unsigned long address,
	struct vm_area_struct **previous);
typedef struct page *(*_neverc_krt_ptmap_vm_normal_page_fn)(
	struct vm_area_struct *vma, unsigned long address, pte_t pte);
typedef int (*_neverc_krt_ptmap_access_remote_vm_fn)(
	struct mm_struct *mm, unsigned long address, void *buffer, int length,
	unsigned int gup_flags);
typedef void (*_neverc_krt_ptmap_mm_ref_fn)(struct mm_struct *mm);
typedef void *(*_neverc_krt_ptmap_heap_alloc_fn)(size_t size, gfp_t flags);

struct _neverc_krt_ptmap_original_pin {
	struct page *page;
	unsigned long pinned_pfn;
	unsigned long snapshot_pfn;
	void *snapshot_address;
};

struct _neverc_krt_ptmap_production_state {
	const struct neverc_krt_gki_layout *layout;
	struct _neverc_krt_ptmap_profile_policy profile_policy;
	const struct _neverc_krt_ptmap_profile_policy *policy;
	_neverc_krt_ptmap_geometry_t geometry;
	_neverc_krt_ptmap_backend_t backend;
	_neverc_krt_ptmap_follow_pte_fn follow_pte;
	_neverc_krt_ptmap_pte_offset_map_lock_fn pte_offset_map_lock;
	_neverc_krt_ptmap_pin_with_vmas_fn pin_with_vmas;
	_neverc_krt_ptmap_pin_without_vmas_fn pin_without_vmas;
	_neverc_krt_ptmap_unpin_fn unpin_user_page;
	_neverc_krt_ptmap_alloc_pages_fn alloc_pages;
	_neverc_krt_ptmap_free_pages_fn free_pages;
	_neverc_krt_ptmap_rwsem_fn down_read;
	_neverc_krt_ptmap_rwsem_fn up_read;
	_neverc_krt_ptmap_rwsem_trylock_fn down_read_trylock;
	_neverc_krt_ptmap_spin_unlock_fn raw_spin_unlock;
	_neverc_krt_ptmap_rcu_fn rcu_read_lock;
	_neverc_krt_ptmap_rcu_fn rcu_read_unlock;
	_neverc_krt_ptmap_find_vma_fn find_vma;
	_neverc_krt_ptmap_find_vma_prev_fn find_vma_prev;
	_neverc_krt_ptmap_vm_normal_page_fn vm_normal_page;
	_neverc_krt_ptmap_access_remote_vm_fn access_remote_vm;
	_neverc_krt_ptmap_mm_ref_fn mmput;
	_neverc_krt_ptmap_mm_ref_fn mmdrop;
	_neverc_krt_ptmap_heap_alloc_fn heap_alloc;
};

static struct _neverc_krt_ptmap_production_state
	_neverc_krt_ptmap_production;
/* 0=uninitialized, 1=one initializer active, 2=published, -1=rejected. */
static int _neverc_krt_ptmap_production_init_state;

static void *_neverc_krt_ptmap_zalloc(size_t size)
{
	_neverc_krt_ptmap_heap_alloc_fn heap_alloc =
		_neverc_krt_ptmap_production.heap_alloc;

	if (!heap_alloc)
		return (void *)0;
	return heap_alloc(size, GFP_KERNEL | __GFP_ZERO);
}

#define _NEVERC_KRT_PTMAP_PTE_VALID       (1ULL << 0)
#define _NEVERC_KRT_PTMAP_PTE_USER        (1ULL << 6)
#define _NEVERC_KRT_PTMAP_PTE_RDONLY      (1ULL << 7)
#define _NEVERC_KRT_PTMAP_PTE_NG          (1ULL << 11)
#define _NEVERC_KRT_PTMAP_PTE_WRITE       (1ULL << 51)
#define _NEVERC_KRT_PTMAP_PTE_PXN         (1ULL << 53)
#define _NEVERC_KRT_PTMAP_PTE_UXN         (1ULL << 54)
#define _NEVERC_KRT_PTMAP_PTE_SPECIAL     (1ULL << 56)
#define _NEVERC_KRT_PTMAP_PTE_TABLE       3ULL
#define _NEVERC_KRT_PTMAP_PTE_TYPE_MASK   3ULL

static __always_inline u64 _neverc_krt_ptmap_read_tcr_el1(void)
{
	u64 value;

	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(value));
	return value;
}

static int _neverc_krt_ptmap_production_layout_valid(
	const struct neverc_krt_gki_layout *layout,
	const struct _neverc_krt_ptmap_profile_policy *policy)
{
	if (!layout || !policy ||
	    !_neverc_krt_ptmap_size_fits(layout->mm_size, layout->mm_count,
					 layout->mm_count_size) ||
	    !_neverc_krt_ptmap_size_fits(layout->mm_size, layout->mm_pgd,
					 layout->mm_pgd_size) ||
	    !_neverc_krt_ptmap_size_fits(layout->mm_size,
					 layout->mm_page_table_lock,
					 layout->mm_page_table_lock_size) ||
	    !_neverc_krt_ptmap_size_fits(layout->mm_size,
					 layout->mm_mmap_lock,
					 layout->mm_mmap_lock_size) ||
	    !_neverc_krt_ptmap_size_fits(layout->vma_size, layout->vma_start,
					 layout->vma_start_size) ||
	    !_neverc_krt_ptmap_size_fits(layout->vma_size, layout->vma_end,
					 layout->vma_end_size) ||
	    !_neverc_krt_ptmap_size_fits(layout->task_size, layout->task_mm,
					 sizeof(void *)) ||
	    layout->mm_count_size != sizeof(u32) ||
	    layout->mm_pgd_size != sizeof(void *) ||
	    layout->mm_page_table_lock_size != sizeof(spinlock_t) ||
	    layout->mm_mmap_lock_size != sizeof(struct rw_semaphore) ||
	    layout->vma_start_size != sizeof(unsigned long) ||
	    layout->vma_end_size != sizeof(unsigned long) ||
	    layout->user_page_shift != 12 || layout->user_va_bits != 39 ||
	    layout->user_pa_bits != 48 || layout->user_pgtable_levels != 3 ||
	    layout->user_pgd_shift != 30 || layout->user_pmd_shift != 21 ||
	    layout->user_pte_shift != 12 || layout->user_index_bits != 9 ||
	    layout->user_contiguous_bit != 52 ||
	    layout->user_contiguous_entries != 16 ||
	    layout->user_descriptor_address_mask !=
		policy->descriptor_address_mask ||
	    layout->user_physical_address_mask !=
		policy->physical_address_mask ||
	    layout->user_physical_page_mask != policy->physical_page_mask ||
	    layout->user_tlbi_all_asid != 1)
		return 0;
	return 1;
}

static int _neverc_krt_ptmap_production_mmap_read_begin(void *opaque_mm)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct rw_semaphore *lock;

	if (!opaque_mm)
		return -EINVAL;
	lock = (struct rw_semaphore *)((unsigned char *)opaque_mm +
					state->layout->mm_mmap_lock);
	state->down_read(lock);
	return 0;
}

static void _neverc_krt_ptmap_production_mmap_read_end(void *opaque_mm)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct rw_semaphore *lock =
		(struct rw_semaphore *)((unsigned char *)opaque_mm +
					state->layout->mm_mmap_lock);

	state->up_read(lock);
}

static int _neverc_krt_ptmap_production_mapping_validate(
	void *opaque_mm, unsigned long address)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct mm_struct *mm = (struct mm_struct *)opaque_mm;
	struct vm_area_struct *vma;
	struct vm_area_struct *previous = (struct vm_area_struct *)0;
	unsigned long start;
	unsigned long end;

	if (state->find_vma)
		vma = state->find_vma(mm, address);
	else
		vma = state->find_vma_prev(mm, address, &previous);
	if (!vma || neverc_krt_mem_read(
			&start, (const unsigned char *)vma +
				state->layout->vma_start, sizeof(start)) ||
	    neverc_krt_mem_read(&end, (const unsigned char *)vma +
				state->layout->vma_end, sizeof(end)) ||
	    start > address || end < address ||
	    end - address < state->geometry.page_size)
		return -EFAULT;
	return 0;
}

static int _neverc_krt_ptmap_production_mm_count_grab(void *opaque_mm)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	int *count = (int *)((unsigned char *)opaque_mm +
				 state->layout->mm_count);
	int observed = __atomic_load_n(count, __ATOMIC_ACQUIRE);
	int desired;

	for (;;) {
		if (observed <= 0 || observed == 0x7fffffff)
			return -EFAULT;
		desired = observed + 1;
		if (__atomic_compare_exchange_n(count, &observed, desired, 1,
						__ATOMIC_RELAXED,
						__ATOMIC_ACQUIRE))
			return 0;
	}
}

static void _neverc_krt_ptmap_production_mm_users_put(void *opaque_mm)
{
	_neverc_krt_ptmap_production.mmput((struct mm_struct *)opaque_mm);
}

static void _neverc_krt_ptmap_production_mm_count_drop(void *opaque_mm)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	int *count = (int *)((unsigned char *)opaque_mm +
				 state->layout->mm_count);
	int observed = __atomic_load_n(count, __ATOMIC_ACQUIRE);
	int desired;

	for (;;) {
		/* A corrupt count is leaked rather than underflowed. */
		if (observed <= 0)
			return;
		desired = observed - 1;
		if (!__atomic_compare_exchange_n(count, &observed, desired, 1,
						 __ATOMIC_RELEASE,
						 __ATOMIC_ACQUIRE))
			continue;
		if (!desired) {
			__atomic_thread_fence(__ATOMIC_ACQUIRE);
			state->mmdrop((struct mm_struct *)opaque_mm);
		}
		return;
	}
}

static int _neverc_krt_ptmap_production_matches_current_mm(void *opaque_mm)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct mm_struct *current_mm = (struct mm_struct *)0;
	struct task_struct *task = current;

	if (!task || neverc_krt_mem_read(
			&current_mm, (const unsigned char *)task +
				state->layout->task_mm, sizeof(current_mm)))
		return -EFAULT;
	return current_mm == opaque_mm ? 1 : 0;
}

static int _neverc_krt_ptmap_production_private_page_alloc(
	unsigned long *pfn, void **page_address)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	unsigned long address;
	unsigned long physical;

	if (!pfn || !page_address)
		return -EINVAL;
	*pfn = 0;
	*page_address = (void *)0;
	address = state->alloc_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (!address)
		return -ENOMEM;
	physical = neverc_krt_virt_to_phys(address);
	if (!physical || (physical & (state->geometry.page_size - 1U)) ||
	    (physical & ~state->geometry.physical_address_mask)) {
		state->free_pages(address, 0);
		return -EFAULT;
	}
	*pfn = physical >> state->geometry.page_shift;
	*page_address = (void *)address;
	return 0;
}

static void _neverc_krt_ptmap_production_private_page_free(
	unsigned long pfn, void *page_address)
{
	(void)pfn;
	if (page_address)
		_neverc_krt_ptmap_production.free_pages(
			(unsigned long)page_address, 0);
}

static int _neverc_krt_ptmap_production_original_page_pin(
	void *opaque_mm, unsigned long address, unsigned long expected_pfn,
	_neverc_krt_ptmap_pte_t expected_descriptor, void **pin_token)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct mm_struct *mm = (struct mm_struct *)opaque_mm;
	struct _neverc_krt_ptmap_original_pin *token;
	struct vm_area_struct *previous = (struct vm_area_struct *)0;
	struct vm_area_struct *vma;
	struct page *expected_page;
	pte_t expected_pte = { .pte = expected_descriptor };
	long pinned;
	int result;

	if (!opaque_mm || !pin_token)
		return -EINVAL;
	*pin_token = (void *)0;
	vma = state->find_vma ? state->find_vma(mm, address) :
		state->find_vma_prev(mm, address, &previous);
	if (!vma)
		return -EFAULT;
	expected_page = state->vm_normal_page(vma, address, expected_pte);
	if (!expected_page)
		return -EOPNOTSUPP;
	token = _neverc_krt_ptmap_zalloc(sizeof(*token));
	if (!token)
		return -ENOMEM;
	if (state->policy->pin_abi == _NEVERC_KRT_PTMAP_PIN_WITH_VMAS)
		pinned = state->pin_with_vmas(
			mm, address, 1, 0,
			&token->page, (struct vm_area_struct **)0, (int *)0);
	else
		pinned = state->pin_without_vmas(
			mm, address, 1, 0,
			&token->page, (int *)0);
	if (pinned != 1 || !token->page) {
		result = pinned < 0 ? (int)pinned : -EFAULT;
		goto fail;
	}
	if (token->page != expected_page) {
		result = -EAGAIN;
		goto fail;
	}
	token->pinned_pfn = expected_pfn;
	*pin_token = token;
	return 0;

fail:
	if (token->page)
		state->unpin_user_page(token->page);
	_neverc_krt_ptmap_free(token);
	return result;
}

static int _neverc_krt_ptmap_production_original_page_snapshot(
	void *opaque_mm, unsigned long address, void *pin_token,
	void **page_address)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct _neverc_krt_ptmap_original_pin *token =
		(struct _neverc_krt_ptmap_original_pin *)pin_token;
	int copied;
	int result;

	if (!opaque_mm || !token || !token->page || !page_address)
		return -EINVAL;
	*page_address = (void *)0;
	result = _neverc_krt_ptmap_production_private_page_alloc(
		&token->snapshot_pfn, &token->snapshot_address);
	if (result)
		return result;
	/*
	 * Copy the pinned page through the linear map.  The user VA can
	 * change after mmap_lock drops; the pin keeps this PFN alive.
	 */
	if (token->pinned_pfn) {
		void *source = (void *)neverc_krt_phys_to_virt(
			token->pinned_pfn << state->geometry.page_shift);

		if (source &&
		    !neverc_krt_mem_read(token->snapshot_address, source,
					 state->geometry.page_size)) {
			*page_address = token->snapshot_address;
			return 0;
		}
	}
	copied = state->access_remote_vm(
		(struct mm_struct *)opaque_mm, address, token->snapshot_address,
		(int)state->geometry.page_size, 0);
	if (copied != (int)state->geometry.page_size) {
		result = copied < 0 ? copied : -EFAULT;
		_neverc_krt_ptmap_production_private_page_free(
			token->snapshot_pfn, token->snapshot_address);
		token->snapshot_pfn = 0;
		token->snapshot_address = (void *)0;
		return result;
	}
	*page_address = token->snapshot_address;
	return 0;
}

static void _neverc_krt_ptmap_production_original_page_unpin(
	void *opaque_mm, unsigned long address, unsigned long expected_pfn,
	void *pin_token, void *page_address)
{
	struct _neverc_krt_ptmap_original_pin *token =
		(struct _neverc_krt_ptmap_original_pin *)pin_token;

	(void)opaque_mm;
	(void)address;
	(void)expected_pfn;
	if (!token)
		return;
	if (token->page)
		_neverc_krt_ptmap_production.unpin_user_page(token->page);
	if (token->snapshot_address &&
	    token->snapshot_address == page_address)
		_neverc_krt_ptmap_production_private_page_free(
			token->snapshot_pfn, token->snapshot_address);
	_neverc_krt_ptmap_free(token);
}

static int _neverc_krt_ptmap_production_rcu_read_begin(void)
{
	_neverc_krt_ptmap_production.rcu_read_lock();
	return 0;
}

static void _neverc_krt_ptmap_production_rcu_read_end(void)
{
	_neverc_krt_ptmap_production.rcu_read_unlock();
}

static int _neverc_krt_ptmap_production_manual_pmd(
	struct mm_struct *mm, unsigned long address, pmd_t **pmd_out)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	unsigned long index_mask =
		(1UL << state->layout->user_index_bits) - 1UL;
	void *pgd = (void *)0;
	u64 pgd_descriptor;
	u64 pmd_descriptor;
	unsigned long table_physical;
	unsigned long table_virtual;
	pmd_t *pmd;

	*pmd_out = (pmd_t *)0;
	if (neverc_krt_mem_read(&pgd, (const unsigned char *)mm +
				state->layout->mm_pgd, sizeof(pgd)) ||
	    !_neverc_krt_kernel_pointer_is_valid(pgd))
		return -EFAULT;
	if (neverc_krt_mem_read(
			&pgd_descriptor,
			(const u64 *)pgd +
				((address >> state->layout->user_pgd_shift) &
				 index_mask),
			sizeof(pgd_descriptor)) ||
	    (pgd_descriptor & _NEVERC_KRT_PTMAP_PTE_TYPE_MASK) !=
		_NEVERC_KRT_PTMAP_PTE_TABLE)
		return -EFAULT;
	table_physical = (unsigned long)(pgd_descriptor &
					 state->layout->user_physical_page_mask);
	table_virtual = neverc_krt_phys_to_virt(table_physical);
	if (!table_physical ||
	    !_neverc_krt_kernel_pointer_is_valid((void *)table_virtual))
		return -EFAULT;
	pmd = (pmd_t *)table_virtual +
		((address >> state->layout->user_pmd_shift) & index_mask);
	if (neverc_krt_mem_read(&pmd_descriptor, pmd,
				sizeof(pmd_descriptor)) ||
	    (pmd_descriptor & _NEVERC_KRT_PTMAP_PTE_TYPE_MASK) !=
		_NEVERC_KRT_PTMAP_PTE_TABLE)
		return -EOPNOTSUPP;
	*pmd_out = pmd;
	return 0;
}

static int _neverc_krt_ptmap_production_pte_lock(
	void *opaque_mm, unsigned long address,
	_neverc_krt_ptmap_pte_window_t *window)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	struct mm_struct *mm = (struct mm_struct *)opaque_mm;
	struct spinlock *ptl = (struct spinlock *)0;
	pte_t *ptep = (pte_t *)0;
	pmd_t *pmd;
	size_t target_index;
	unsigned int incoming_flags;
	int result;

	incoming_flags = window->backend_flags;
	__builtin_memset(window, 0, sizeof(*window));
	if (state->policy->pte_route == _NEVERC_KRT_PTMAP_FOLLOW_PTE) {
		struct rw_semaphore *mmap_lock =
			(struct rw_semaphore *)((unsigned char *)mm +
						state->layout->mm_mmap_lock);

		/*
		 * follow_pte() requires mmap_lock.  Install already holds it.
		 * Prepared ops must not sleep, so only a trylock is allowed.
		 */
		if (!(incoming_flags & _NEVERC_KRT_PTMAP_WINDOW_MMAP_PREHELD)) {
			if (!state->down_read_trylock ||
			    !state->down_read_trylock(mmap_lock))
				return -EBUSY;
			window->backend_flags |=
				_NEVERC_KRT_PTMAP_WINDOW_HELD_MMAP;
		}
		result = state->follow_pte(mm, address, &ptep, &ptl);
		if (result) {
			if (window->backend_flags &
			    _NEVERC_KRT_PTMAP_WINDOW_HELD_MMAP)
				state->up_read(mmap_lock);
			return result;
		}
	} else {
		result = _neverc_krt_ptmap_production_manual_pmd(
			mm, address, &pmd);
		if (result)
			return result;
		ptep = state->pte_offset_map_lock(mm, pmd, address, &ptl);
		if (!ptep)
			return -EFAULT;
	}
	if (!ptl || !ptep) {
		if (ptl)
			state->raw_spin_unlock((struct raw_spinlock *)ptl);
		if (window->backend_flags & _NEVERC_KRT_PTMAP_WINDOW_HELD_MMAP)
			state->up_read(
				(struct rw_semaphore *)((unsigned char *)mm +
							state->layout->mm_mmap_lock));
		if (state->policy->pte_release_internal_rcu)
			state->rcu_read_unlock();
		return -EFAULT;
	}
	target_index = (address >> state->layout->user_pte_shift) &
		(state->layout->user_contiguous_entries - 1UL);
	window->entries = (_neverc_krt_ptmap_pte_t *)ptep - target_index;
	window->entry_count = state->layout->user_contiguous_entries;
	window->target_index = target_index;
	window->group_address = address &
		~((state->geometry.page_size *
		   state->layout->user_contiguous_entries) - 1UL);
	window->lock_token = ptl;
	if (state->policy->pte_release_internal_rcu)
		window->backend_flags |=
			_NEVERC_KRT_PTMAP_WINDOW_INTERNAL_RCU;
	return 0;
}

static void _neverc_krt_ptmap_production_pte_unlock(
	void *opaque_mm, _neverc_krt_ptmap_pte_window_t *window)
{
	if (window->lock_token)
		_neverc_krt_ptmap_production.raw_spin_unlock(
			(struct raw_spinlock *)window->lock_token);
	if ((window->backend_flags & _NEVERC_KRT_PTMAP_WINDOW_HELD_MMAP) &&
	    opaque_mm && _neverc_krt_ptmap_production.up_read &&
	    _neverc_krt_ptmap_production.layout)
		_neverc_krt_ptmap_production.up_read(
			(struct rw_semaphore *)((unsigned char *)opaque_mm +
						_neverc_krt_ptmap_production.layout
							->mm_mmap_lock));
	if (window->backend_flags &
	    _NEVERC_KRT_PTMAP_WINDOW_INTERNAL_RCU)
		_neverc_krt_ptmap_production.rcu_read_unlock();
	window->lock_token = (void *)0;
	window->backend_flags = 0;
}

static int _neverc_krt_ptmap_production_pte_write(
	_neverc_krt_ptmap_pte_t *entry, _neverc_krt_ptmap_pte_t value)
{
	if (!entry)
		return -EINVAL;
	__atomic_store_n(entry, value, __ATOMIC_RELEASE);
	return 0;
}

static void _neverc_krt_ptmap_production_tlbi_range(
	unsigned long address, unsigned int page_count)
{
	unsigned int i;

	if (!page_count)
		return;
	__asm__ __volatile__("dsb ishst" : : : "memory");
	for (i = 0; i < page_count; i++) {
		unsigned long operand =
			((address + ((unsigned long)i << 12)) >> 12) &
			((1UL << 44) - 1UL);

		/* VAAE1IS invalidates this VA for every ASID.  Using VALE1IS here
		 * would accidentally target ASID zero only. */
		__asm__ __volatile__("tlbi vaae1is, %0" : : "r"(operand) :
				     "memory");
	}
	__asm__ __volatile__("dsb ish\nisb" : : : "memory");
}

static void _neverc_krt_ptmap_production_sync_exec_page(
	void *page_address, size_t size)
{
	unsigned long ctr;
	unsigned long dline;
	unsigned long iline;
	unsigned long start = (unsigned long)page_address;
	unsigned long end = start + size;
	unsigned long cursor;

	if (!page_address || !size || end < start)
		return;
	__asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
	dline = 4UL << ((ctr >> 16) & 0xfUL);
	iline = 4UL << (ctr & 0xfUL);
	for (cursor = start & ~(dline - 1UL); cursor < end;
	     cursor += dline)
		__asm__ __volatile__("dc cvau, %0" : : "r"(cursor) : "memory");
	__asm__ __volatile__("dsb ish" : : : "memory");
	for (cursor = start & ~(iline - 1UL); cursor < end;
	     cursor += iline)
		__asm__ __volatile__("ic ivau, %0" : : "r"(cursor) : "memory");
	__asm__ __volatile__("dsb ish\nisb" : : : "memory");
}

static int _neverc_krt_ptmap_production_nofault_read(
	void *destination, const void *source, size_t size)
{
	return neverc_krt_mem_read(destination, source, size) ? -EFAULT : 0;
}

static int _neverc_krt_ptmap_production_nofault_write(
	void *destination, const void *source, size_t size)
{
	return neverc_krt_mem_write(destination, source, size) ? -EFAULT : 0;
}

/*
 * Address-taking keeps a compiler-derived KCFI type anchor for the stable
 * vm_normal_page KMI.  Comparing against this tag avoids another profile-local
 * magic hash while still failing closed before the indirect call.
 */
static noinline __used struct page *
_neverc_krt_ptmap_vm_normal_page_type(
	struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
	(void)vma;
	(void)address;
	(void)pte;
	return (struct page *)0;
}

static noinline __used int
_neverc_krt_ptmap_down_read_trylock_type(struct rw_semaphore *sem)
{
	(void)sem;
	return 0;
}

static int _neverc_krt_ptmap_production_kcfi_valid(
	struct _neverc_krt_ptmap_production_state *state, unsigned int mode,
	u32 *observed_tags)
{
	const struct _neverc_krt_ptmap_profile_policy *policy = state->policy;
	void *pte_target = policy->pte_route == _NEVERC_KRT_PTMAP_FOLLOW_PTE ?
		(void *)state->follow_pte : (void *)state->pte_offset_map_lock;
	void *pin_target = policy->pin_abi ==
		_NEVERC_KRT_PTMAP_PIN_WITH_VMAS ?
		(void *)state->pin_with_vmas : (void *)state->pin_without_vmas;
	void *find_target = state->find_vma ? (void *)state->find_vma :
		(void *)state->find_vma_prev;

	if (mode == _NEVERC_KRT_PTMAP_KCFI_DISABLED)
		return _neverc_krt_ptmap_runtime_gate(
			policy, _neverc_krt_ptmap_read_tcr_el1(), mode,
			(const u32 *)0, 0);
	if (neverc_krt_cfi_read_tag((void *)state->vm_normal_page) !=
	    neverc_krt_cfi_read_tag(
		    (void *)_neverc_krt_ptmap_vm_normal_page_type))
		return -EOPNOTSUPP;
	observed_tags[0] = neverc_krt_cfi_read_tag(pte_target);
	observed_tags[1] = neverc_krt_cfi_read_tag(pin_target);
	observed_tags[2] = neverc_krt_cfi_read_tag(
		(void *)state->unpin_user_page);
	observed_tags[3] = neverc_krt_cfi_read_tag((void *)state->alloc_pages);
	observed_tags[4] = neverc_krt_cfi_read_tag((void *)state->free_pages);
	observed_tags[5] = neverc_krt_cfi_read_tag((void *)state->down_read);
	observed_tags[6] = neverc_krt_cfi_read_tag(
		(void *)state->raw_spin_unlock);
	observed_tags[7] = neverc_krt_cfi_read_tag((void *)state->rcu_read_lock);
	observed_tags[8] = neverc_krt_cfi_read_tag(find_target);
	observed_tags[9] = neverc_krt_cfi_read_tag(
		(void *)state->access_remote_vm);
	observed_tags[10] = neverc_krt_cfi_read_tag((void *)state->mmput);
	observed_tags[11] = neverc_krt_cfi_read_tag(
		(void *)state->heap_alloc);
	if (_neverc_krt_ptmap_runtime_gate(
			policy, _neverc_krt_ptmap_read_tcr_el1(), mode,
			observed_tags, _NEVERC_KRT_PTMAP_KCFI_TAG_COUNT))
		return -EOPNOTSUPP;
	/* Same-prototype pairs must each carry the certified tag. */
	if (neverc_krt_cfi_read_tag((void *)state->up_read) !=
		policy->kcfi_rwsem ||
	    neverc_krt_cfi_read_tag((void *)state->rcu_read_unlock) !=
		policy->kcfi_rcu ||
	    neverc_krt_cfi_read_tag((void *)state->mmdrop) !=
		policy->kcfi_mmput)
		return -EOPNOTSUPP;
	if (state->down_read_trylock &&
	    neverc_krt_cfi_read_tag((void *)state->down_read_trylock) !=
		neverc_krt_cfi_read_tag(
			(void *)_neverc_krt_ptmap_down_read_trylock_type))
		return -EOPNOTSUPP;
	return 0;
}

static int _neverc_krt_ptmap_production_resolve(
	struct _neverc_krt_ptmap_production_state *state)
{
	const struct _neverc_krt_ptmap_profile_policy *policy = state->policy;

	if (policy->pte_route == _NEVERC_KRT_PTMAP_FOLLOW_PTE)
		state->follow_pte = (_neverc_krt_ptmap_follow_pte_fn)
			NEVERC_KRT_LOOKUP("follow_pte");
	else
		state->pte_offset_map_lock =
			(_neverc_krt_ptmap_pte_offset_map_lock_fn)
			NEVERC_KRT_LOOKUP("__pte_offset_map_lock");
	if (policy->pin_abi == _NEVERC_KRT_PTMAP_PIN_WITH_VMAS)
		state->pin_with_vmas = (_neverc_krt_ptmap_pin_with_vmas_fn)
			NEVERC_KRT_LOOKUP("pin_user_pages_remote");
	else
		state->pin_without_vmas =
			(_neverc_krt_ptmap_pin_without_vmas_fn)
			NEVERC_KRT_LOOKUP("pin_user_pages_remote");
	state->unpin_user_page = (_neverc_krt_ptmap_unpin_fn)
		NEVERC_KRT_LOOKUP("unpin_user_page");
	if (policy->alloc_abi == _NEVERC_KRT_PTMAP_GET_FREE_PAGES)
		state->alloc_pages = (_neverc_krt_ptmap_alloc_pages_fn)
			NEVERC_KRT_LOOKUP("__get_free_pages");
	else
		state->alloc_pages = (_neverc_krt_ptmap_alloc_pages_fn)
			NEVERC_KRT_LOOKUP("get_free_pages_noprof");
	state->free_pages = (_neverc_krt_ptmap_free_pages_fn)
		NEVERC_KRT_LOOKUP("free_pages");
	state->down_read = (_neverc_krt_ptmap_rwsem_fn)
		NEVERC_KRT_LOOKUP("down_read");
	state->up_read = (_neverc_krt_ptmap_rwsem_fn)
		NEVERC_KRT_LOOKUP("up_read");
	if (policy->pte_route == _NEVERC_KRT_PTMAP_FOLLOW_PTE)
		state->down_read_trylock =
			(_neverc_krt_ptmap_rwsem_trylock_fn)
			NEVERC_KRT_LOOKUP("down_read_trylock");
	state->raw_spin_unlock = (_neverc_krt_ptmap_spin_unlock_fn)
		NEVERC_KRT_LOOKUP("_raw_spin_unlock");
	state->rcu_read_lock = (_neverc_krt_ptmap_rcu_fn)
		NEVERC_KRT_LOOKUP("__rcu_read_lock");
	state->rcu_read_unlock = (_neverc_krt_ptmap_rcu_fn)
		NEVERC_KRT_LOOKUP("__rcu_read_unlock");
	if (policy->use_find_vma_prev)
		state->find_vma_prev = (_neverc_krt_ptmap_find_vma_prev_fn)
			NEVERC_KRT_LOOKUP("find_vma_prev");
	else
		state->find_vma = (_neverc_krt_ptmap_find_vma_fn)
			NEVERC_KRT_LOOKUP("find_vma");
	state->vm_normal_page = (_neverc_krt_ptmap_vm_normal_page_fn)
		NEVERC_KRT_LOOKUP("vm_normal_page");
	state->access_remote_vm = (_neverc_krt_ptmap_access_remote_vm_fn)
		NEVERC_KRT_LOOKUP("access_remote_vm");
	state->mmput = (_neverc_krt_ptmap_mm_ref_fn)
		NEVERC_KRT_LOOKUP("mmput");
	state->mmdrop = (_neverc_krt_ptmap_mm_ref_fn)
		NEVERC_KRT_LOOKUP("__mmdrop");
	if (policy->alloc_abi == _NEVERC_KRT_PTMAP_GET_FREE_PAGES)
		state->heap_alloc = (_neverc_krt_ptmap_heap_alloc_fn)
			NEVERC_KRT_LOOKUP("__kmalloc");
	else
		state->heap_alloc = (_neverc_krt_ptmap_heap_alloc_fn)
			NEVERC_KRT_LOOKUP("__kmalloc_noprof");

	if ((!state->follow_pte && !state->pte_offset_map_lock) ||
	    (state->follow_pte && !state->down_read_trylock) ||
	    (!state->pin_with_vmas && !state->pin_without_vmas) ||
	    !state->unpin_user_page || !state->alloc_pages ||
	    !state->free_pages || !state->down_read || !state->up_read ||
	    !state->raw_spin_unlock || !state->rcu_read_lock ||
	    !state->rcu_read_unlock ||
	    (!state->find_vma && !state->find_vma_prev) ||
	    !state->vm_normal_page ||
	    !state->access_remote_vm || !state->mmput || !state->mmdrop ||
	    !state->heap_alloc)
		return -EOPNOTSUPP;
	return 0;
}

static int _neverc_krt_ptmap_production_initialize(void)
{
	struct _neverc_krt_ptmap_production_state *state =
		&_neverc_krt_ptmap_production;
	const struct neverc_krt_gki_layout *layout;
	const struct neverc_krt_profile *profile;
	struct _neverc_krt_ptmap_profile_policy policy;
	u32 observed_tags[_NEVERC_KRT_PTMAP_KCFI_TAG_COUNT];
	int profile_id;
	int kcfi_mode;

	if (neverc_krt_compat_init() ||
	    !_neverc_krt_layout_fields_proven(
		NEVERC_KRT_LAYOUT_CERT_USER_PTMAP |
		NEVERC_KRT_LAYOUT_CERT_TASK_WALK))
		return -EOPNOTSUPP;
	profile_id = _neverc_krt_current_profile_id();
	profile = profile_id < 0 ? (const struct neverc_krt_profile *)0 :
		neverc_krt_find_profile((unsigned int)profile_id);
	if (!profile || _neverc_krt_ptmap_policy_for_kernel(
			profile->linux_major, profile->linux_minor, &policy))
		return -EOPNOTSUPP;
	layout = _neverc_krt_get_gki_layout();
	if (!_neverc_krt_ptmap_production_layout_valid(layout, &policy))
		return -EOPNOTSUPP;
	if (neverc_krt_mem_init() || !_neverc_krt_mem_nofault_available() ||
	    neverc_krt_addr_init() || neverc_krt_page_shift() != 12 ||
	    !neverc_krt_linmap_available())
		return -EOPNOTSUPP;

	__builtin_memset(state, 0, sizeof(*state));
	state->layout = layout;
	state->profile_policy = policy;
	state->policy = &state->profile_policy;
	state->geometry.page_size = 1UL << layout->user_page_shift;
	state->geometry.page_shift = (unsigned int)layout->user_page_shift;
	state->geometry.contiguous_entries =
		(unsigned int)layout->user_contiguous_entries;
	state->geometry.descriptor_address_mask =
		layout->user_descriptor_address_mask;
	state->geometry.physical_address_mask =
		layout->user_physical_address_mask;
	state->geometry.physical_page_mask =
		layout->user_physical_page_mask;
	state->geometry.valid_mask = _NEVERC_KRT_PTMAP_PTE_VALID;
	state->geometry.user_mask = _NEVERC_KRT_PTMAP_PTE_USER;
	state->geometry.readonly_mask = _NEVERC_KRT_PTMAP_PTE_RDONLY;
	state->geometry.write_mask = _NEVERC_KRT_PTMAP_PTE_WRITE;
	state->geometry.uxn_mask = _NEVERC_KRT_PTMAP_PTE_UXN;
	state->geometry.pxn_mask = _NEVERC_KRT_PTMAP_PTE_PXN;
	state->geometry.ng_mask = _NEVERC_KRT_PTMAP_PTE_NG;
	state->geometry.special_mask = _NEVERC_KRT_PTMAP_PTE_SPECIAL;
	state->geometry.contiguous_mask =
		1ULL << layout->user_contiguous_bit;
	state->geometry.pt_regs_size = layout->pt_regs_size;
	state->geometry.pt_regs_x = layout->pt_regs_regs;
	state->geometry.pt_regs_sp = layout->pt_regs_sp;
	state->geometry.pt_regs_pc = layout->pt_regs_pc;
	state->geometry.pt_regs_pstate = layout->pt_regs_pstate;
	state->geometry.pstate_mode_mask = 0xfULL;
	state->geometry.pstate_user_mode = 0;
	/*
	 * The generated policy's descriptor_address_mask matches the profile that
	 * was pinned at build time, which may be LPA2 (output address in PTE bits
	 * [49:12]).  A COMPAT device can run a different page-table geometry, so
	 * adapt the OA field width to this kernel's live MMU config.  When
	 * TCR_EL1.DS is clear the descriptors are non-LPA2 4KB: the OA is bits
	 * [47:12], SH stays in [9:8] and [49:48] are RES0.  Derive that field from
	 * the policy's own physical_address_mask (its high bits above the page
	 * offset) instead of hardcoding a width, so no non-policy geometry constant
	 * enters the runtime.
	 */
	if (!(_neverc_krt_ptmap_read_tcr_el1() & (1ULL << 59)))
		state->geometry.descriptor_address_mask =
			state->geometry.physical_address_mask &
			~(state->geometry.page_size - 1ULL);
	if (!_neverc_krt_ptmap_geometry_valid(&state->geometry) ||
	    _neverc_krt_ptmap_production_resolve(state))
		return -EOPNOTSUPP;
	kcfi_mode = _neverc_krt_current_kcfi_mode();
	if (_neverc_krt_ptmap_production_kcfi_valid(
			state, (unsigned int)kcfi_mode, observed_tags))
		return -EOPNOTSUPP;

	state->backend.geometry = &state->geometry;
	state->backend.mmap_read_begin =
		_neverc_krt_ptmap_production_mmap_read_begin;
	state->backend.mmap_read_end =
		_neverc_krt_ptmap_production_mmap_read_end;
	state->backend.mapping_validate =
		_neverc_krt_ptmap_production_mapping_validate;
	state->backend.mm_count_grab =
		_neverc_krt_ptmap_production_mm_count_grab;
	state->backend.mm_users_put =
		_neverc_krt_ptmap_production_mm_users_put;
	state->backend.mm_count_drop =
		_neverc_krt_ptmap_production_mm_count_drop;
	state->backend.matches_current_mm =
		_neverc_krt_ptmap_production_matches_current_mm;
	state->backend.original_page_pin =
		_neverc_krt_ptmap_production_original_page_pin;
	state->backend.original_page_snapshot =
		_neverc_krt_ptmap_production_original_page_snapshot;
	state->backend.original_page_unpin =
		_neverc_krt_ptmap_production_original_page_unpin;
	state->backend.private_page_alloc =
		_neverc_krt_ptmap_production_private_page_alloc;
	state->backend.private_page_free =
		_neverc_krt_ptmap_production_private_page_free;
	state->backend.rcu_read_begin =
		_neverc_krt_ptmap_production_rcu_read_begin;
	state->backend.rcu_read_end =
		_neverc_krt_ptmap_production_rcu_read_end;
	state->backend.pte_lock = _neverc_krt_ptmap_production_pte_lock;
	state->backend.pte_unlock = _neverc_krt_ptmap_production_pte_unlock;
	state->backend.pte_write = _neverc_krt_ptmap_production_pte_write;
	state->backend.tlbi_range = _neverc_krt_ptmap_production_tlbi_range;
	state->backend.sync_exec_page =
		_neverc_krt_ptmap_production_sync_exec_page;
	state->backend.nofault_read =
		_neverc_krt_ptmap_production_nofault_read;
	state->backend.nofault_write =
		_neverc_krt_ptmap_production_nofault_write;
	if (!_neverc_krt_ptmap_backend_valid(&state->backend))
		return -EOPNOTSUPP;
	__atomic_store_n(&_neverc_krt_ptmap_backend, &state->backend,
			 __ATOMIC_RELEASE);
	return 0;
}

static int _neverc_krt_ptmap_production_ensure(void)
{
	int state = __atomic_load_n(&_neverc_krt_ptmap_production_init_state,
				    __ATOMIC_ACQUIRE);
	int expected;
	int result;

	if (state == 2)
		return 1;
	if (state < 0)
		return 0;
	expected = 0;
	if (!__atomic_compare_exchange_n(
			&_neverc_krt_ptmap_production_init_state, &expected, 1, 0,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return 0;
	result = _neverc_krt_ptmap_production_initialize();
	__atomic_store_n(&_neverc_krt_ptmap_production_init_state,
			 result ? -1 : 2, __ATOMIC_RELEASE);
	return result ? 0 : 1;
}

#endif /* !NEVERC_KRT_USER_PTMAP_HOST_TEST */

int neverc_krt_user_ptmap_busy(void)
{
	return (__atomic_load_n(&_neverc_krt_ptmap_active_maps,
				__ATOMIC_ACQUIRE) ||
		__atomic_load_n(&_neverc_krt_ptmap_pending_installs,
				__ATOMIC_ACQUIRE)) ? 1 : 0;
}

int _neverc_krt_user_ptmap_claim_cleanup(void)
{
	if (__atomic_exchange_n(&_neverc_krt_ptmap_cleanup_blocked, 1U,
				__ATOMIC_ACQ_REL))
		return -EBUSY;
	if (neverc_krt_user_ptmap_busy()) {
		__atomic_store_n(&_neverc_krt_ptmap_cleanup_blocked, 0U,
				 __ATOMIC_RELEASE);
		return -EBUSY;
	}
	return 0;
}

void _neverc_krt_user_ptmap_release_cleanup(void)
{
	__atomic_store_n(&_neverc_krt_ptmap_cleanup_blocked, 0U,
			 __ATOMIC_RELEASE);
}

int neverc_krt_user_ptmap_available(void)
{
	const _neverc_krt_ptmap_backend_t *backend =
		__atomic_load_n(&_neverc_krt_ptmap_backend, __ATOMIC_ACQUIRE);

#if !defined(NEVERC_KRT_USER_PTMAP_HOST_TEST)
	if (!_neverc_krt_ptmap_backend_valid(backend) &&
	    _neverc_krt_ptmap_production_ensure())
		backend = __atomic_load_n(&_neverc_krt_ptmap_backend,
					  __ATOMIC_ACQUIRE);
#endif

	return _neverc_krt_ptmap_backend_valid(backend) ? 1 : 0;
}

#if defined(NEVERC_KRT_USER_PTMAP_HOST_TEST)
int neverc_krt_user_ptmap_test_bind_backend(
	const struct neverc_krt_user_ptmap_test_backend *backend)
{
	const struct neverc_krt_user_ptmap_test_geometry *geometry;

	if (!backend || !(geometry = backend->geometry) ||
	    !backend->mmap_read_begin || !backend->mmap_read_end ||
	    !backend->mapping_validate ||
	    !backend->mm_count_grab || !backend->mm_users_put ||
	    !backend->mm_count_drop || !backend->matches_current_mm ||
	    !backend->original_page_get || !backend->original_page_put ||
	    !backend->private_page_alloc || !backend->private_page_free ||
	    !backend->rcu_read_begin || !backend->rcu_read_end ||
	    !backend->pte_lock || !backend->pte_unlock || !backend->pte_write ||
	    !backend->tlbi_range || !backend->sync_exec_page ||
	    !backend->nofault_read || !backend->nofault_write)
		return -EINVAL;
	if (neverc_krt_user_ptmap_busy() ||
	    __atomic_load_n(&_neverc_krt_ptmap_cleanup_blocked,
			    __ATOMIC_ACQUIRE))
		return -EBUSY;

	_neverc_krt_ptmap_host_geometry.page_size = geometry->page_size;
	_neverc_krt_ptmap_host_geometry.page_shift = geometry->page_shift;
	_neverc_krt_ptmap_host_geometry.contiguous_entries =
		geometry->contiguous_entries;
	_neverc_krt_ptmap_host_geometry.descriptor_address_mask =
		geometry->descriptor_address_mask;
	_neverc_krt_ptmap_host_geometry.physical_address_mask =
		geometry->physical_address_mask;
	_neverc_krt_ptmap_host_geometry.physical_page_mask =
		geometry->physical_page_mask;
	_neverc_krt_ptmap_host_geometry.valid_mask = geometry->valid_mask;
	_neverc_krt_ptmap_host_geometry.user_mask = geometry->user_mask;
	_neverc_krt_ptmap_host_geometry.readonly_mask = geometry->readonly_mask;
	_neverc_krt_ptmap_host_geometry.write_mask = geometry->write_mask;
	_neverc_krt_ptmap_host_geometry.uxn_mask = geometry->uxn_mask;
	_neverc_krt_ptmap_host_geometry.pxn_mask = geometry->pxn_mask;
	_neverc_krt_ptmap_host_geometry.ng_mask = geometry->ng_mask;
	_neverc_krt_ptmap_host_geometry.special_mask = geometry->special_mask;
	_neverc_krt_ptmap_host_geometry.contiguous_mask =
		geometry->contiguous_mask;
	_neverc_krt_ptmap_host_geometry.pt_regs_size = geometry->pt_regs_size;
	_neverc_krt_ptmap_host_geometry.pt_regs_x = geometry->pt_regs_x;
	_neverc_krt_ptmap_host_geometry.pt_regs_sp = geometry->pt_regs_sp;
	_neverc_krt_ptmap_host_geometry.pt_regs_pc = geometry->pt_regs_pc;
	_neverc_krt_ptmap_host_geometry.pt_regs_pstate = geometry->pt_regs_pstate;
	_neverc_krt_ptmap_host_geometry.pstate_mode_mask =
		geometry->pstate_mode_mask;
	_neverc_krt_ptmap_host_geometry.pstate_user_mode =
		geometry->pstate_user_mode;

	_neverc_krt_ptmap_host_backend = backend;
	_neverc_krt_ptmap_host_adapter.geometry =
		&_neverc_krt_ptmap_host_geometry;
	_neverc_krt_ptmap_host_adapter.mmap_read_begin =
		backend->mmap_read_begin;
	_neverc_krt_ptmap_host_adapter.mmap_read_end = backend->mmap_read_end;
	_neverc_krt_ptmap_host_adapter.mapping_validate =
		backend->mapping_validate;
	_neverc_krt_ptmap_host_adapter.mm_count_grab = backend->mm_count_grab;
	_neverc_krt_ptmap_host_adapter.mm_users_put = backend->mm_users_put;
	_neverc_krt_ptmap_host_adapter.mm_count_drop = backend->mm_count_drop;
	_neverc_krt_ptmap_host_adapter.matches_current_mm =
		backend->matches_current_mm;
	_neverc_krt_ptmap_host_adapter.original_page_pin =
		_neverc_krt_ptmap_host_original_pin;
	_neverc_krt_ptmap_host_adapter.original_page_snapshot =
		_neverc_krt_ptmap_host_original_snapshot;
	_neverc_krt_ptmap_host_adapter.original_page_unpin =
		_neverc_krt_ptmap_host_original_unpin;
	_neverc_krt_ptmap_host_adapter.private_page_alloc =
		backend->private_page_alloc;
	_neverc_krt_ptmap_host_adapter.private_page_free =
		backend->private_page_free;
	_neverc_krt_ptmap_host_adapter.rcu_read_begin =
		backend->rcu_read_begin;
	_neverc_krt_ptmap_host_adapter.rcu_read_end = backend->rcu_read_end;
	_neverc_krt_ptmap_host_adapter.pte_lock =
		_neverc_krt_ptmap_host_pte_lock;
	_neverc_krt_ptmap_host_adapter.pte_unlock =
		_neverc_krt_ptmap_host_pte_unlock;
	_neverc_krt_ptmap_host_adapter.pte_write = backend->pte_write;
	_neverc_krt_ptmap_host_adapter.tlbi_range = backend->tlbi_range;
	_neverc_krt_ptmap_host_adapter.sync_exec_page =
		backend->sync_exec_page;
	_neverc_krt_ptmap_host_adapter.nofault_read = backend->nofault_read;
	_neverc_krt_ptmap_host_adapter.nofault_write = backend->nofault_write;
	if (!_neverc_krt_ptmap_backend_valid(
			&_neverc_krt_ptmap_host_adapter)) {
		_neverc_krt_ptmap_host_backend = (void *)0;
		return -EINVAL;
	}
	__atomic_store_n(&_neverc_krt_ptmap_backend,
			 &_neverc_krt_ptmap_host_adapter, __ATOMIC_RELEASE);
	return 0;
}

int neverc_krt_user_ptmap_test_release(struct neverc_krt_user_ptmap **map_pointer)
{
	struct neverc_krt_user_ptmap *map;
	int result;

	if (!map_pointer)
		return -EINVAL;
	map = *map_pointer;
	result = _neverc_krt_ptmap_operation_begin(map);
	if (result)
		return result;
	*map_pointer = (void *)0;
	_neverc_krt_ptmap_release(map);
	return 0;
}
#endif

static int _neverc_krt_ptmap_capture_original(
	struct neverc_krt_user_ptmap *map)
{
	const _neverc_krt_ptmap_geometry_t *geometry = map->backend->geometry;
	struct _neverc_krt_ptmap_locked locked;
	_neverc_krt_ptmap_pte_t descriptor;
	int result;

	result = _neverc_krt_ptmap_lock(map, &locked);
	if (result)
		return result;
	descriptor = locked.window.entries[locked.window.target_index];
	if (!(descriptor & geometry->valid_mask) ||
	    !(descriptor & geometry->user_mask)) {
		result = -EFAULT;
		goto out;
	}
	map->original_raw = descriptor;
	map->original_normalized = descriptor & ~geometry->contiguous_mask;
	map->current_descriptor = descriptor;
	map->original.pfn =
		_neverc_krt_ptmap_descriptor_pfn(geometry, descriptor);
	if (!_neverc_krt_ptmap_pfn_encodable(geometry, map->original.pfn)) {
		result = -EFAULT;
		goto out;
	}
	if (descriptor & geometry->contiguous_mask) {
		_neverc_krt_ptmap_pte_t target_attributes =
			descriptor & ~geometry->descriptor_address_mask;
		unsigned long target_pfn = map->original.pfn;
		unsigned long first_pfn;
		unsigned long group_size;
		size_t i;

		if (locked.window.entry_count != geometry->contiguous_entries ||
		    locked.window.target_index >= geometry->contiguous_entries ||
		    geometry->page_size >
			~0UL / geometry->contiguous_entries ||
		    target_pfn < locked.window.target_index) {
			result = -EFAULT;
			goto out;
		}
		group_size = (unsigned long)geometry->contiguous_entries *
			geometry->page_size;
		if (!group_size || locked.window.group_address % group_size) {
			result = -EFAULT;
			goto out;
		}
		first_pfn = target_pfn - locked.window.target_index;
		for (i = 0; i < geometry->contiguous_entries; i++) {
			_neverc_krt_ptmap_pte_t member = locked.window.entries[i];

			if ((member & ~geometry->descriptor_address_mask) !=
			    target_attributes ||
			    _neverc_krt_ptmap_descriptor_pfn(geometry, member) !=
				first_pfn + i) {
				result = -EFAULT;
				goto out;
			}
			map->contiguous_group[i] = member;
		}
		map->contiguous_count = geometry->contiguous_entries;
		map->contiguous_target = locked.window.target_index;
		map->contiguous_address = locked.window.group_address;
		map->contiguous_live = 1;
	}
	result = 0;
out:
	_neverc_krt_ptmap_unlock(map, &locked);
	return result;
}

static int _neverc_krt_ptmap_revalidate_original(
	struct neverc_krt_user_ptmap *map)
{
	struct _neverc_krt_ptmap_locked locked;
	size_t i;
	int result;

	result = _neverc_krt_ptmap_lock(map, &locked);
	if (result)
		return result;
	if (locked.window.entries[locked.window.target_index] !=
	    map->original_raw) {
		result = -EAGAIN;
		goto out;
	}
	if (map->contiguous_live) {
		if (locked.window.entry_count != map->contiguous_count ||
		    locked.window.target_index != map->contiguous_target ||
		    locked.window.group_address != map->contiguous_address) {
			result = -EAGAIN;
			goto out;
		}
		for (i = 0; i < map->contiguous_count; i++) {
			if (locked.window.entries[i] !=
			    map->contiguous_group[i]) {
				result = -EAGAIN;
				goto out;
			}
		}
	}
	result = 0;
out:
	_neverc_krt_ptmap_unlock(map, &locked);
	return result;
}

static int _neverc_krt_ptmap_allocate_private(
	struct neverc_krt_user_ptmap *map,
	struct _neverc_krt_ptmap_backing *backing)
{
	int result;

	result = map->backend->private_page_alloc(&backing->pfn,
						&backing->address);
	if (result)
		return result;
	backing->owned = 1;
	if (!backing->address ||
	    !_neverc_krt_ptmap_pfn_encodable(map->backend->geometry,
					     backing->pfn))
		return -EFAULT;
	return 0;
}

int neverc_krt_user_ptmap_install(
	const struct neverc_krt_user_ptmap_install *request,
	struct neverc_krt_user_ptmap **out_map)
{
	const _neverc_krt_ptmap_backend_t *backend;
	const _neverc_krt_ptmap_geometry_t *geometry;
	struct neverc_krt_user_ptmap *map = (void *)0;
	int mmap_locked = 0;
	int result;

	if (!out_map)
		return -EINVAL;
	*out_map = (void *)0;
#if !defined(NEVERC_KRT_USER_PTMAP_HOST_TEST)
	if (!neverc_krt_user_ptmap_available())
		return -EOPNOTSUPP;
#endif
	backend = __atomic_load_n(&_neverc_krt_ptmap_backend,
				  __ATOMIC_ACQUIRE);
	if (!_neverc_krt_ptmap_backend_valid(backend))
		return -EOPNOTSUPP;
	if (!request || !request->mm ||
	    (request->private_slots != NEVERC_KRT_USER_PTMAP_PRIVATE_ALL &&
	     request->private_slots != NEVERC_KRT_USER_PTMAP_PRIVATE_USER_RW))
		return -EINVAL;
	if (__atomic_load_n(&_neverc_krt_ptmap_cleanup_blocked,
			    __ATOMIC_ACQUIRE))
		return -EBUSY;
	__atomic_fetch_add(&_neverc_krt_ptmap_pending_installs, 1U,
			   __ATOMIC_ACQ_REL);
	if (__atomic_load_n(&_neverc_krt_ptmap_cleanup_blocked,
			    __ATOMIC_ACQUIRE)) {
		__atomic_fetch_sub(&_neverc_krt_ptmap_pending_installs, 1U,
				   __ATOMIC_ACQ_REL);
		return -EBUSY;
	}
	geometry = backend->geometry;
	map = _neverc_krt_ptmap_zalloc(sizeof(*map));
	if (!map) {
		__atomic_fetch_sub(&_neverc_krt_ptmap_pending_installs, 1U,
				   __ATOMIC_ACQ_REL);
		return -ENOMEM;
	}
	map->contiguous_group = _neverc_krt_ptmap_zalloc(
		geometry->contiguous_entries * sizeof(*map->contiguous_group));
	if (!map->contiguous_group) {
		result = -ENOMEM;
		goto fail;
	}
	map->magic = _NEVERC_KRT_PTMAP_MAGIC;
	map->backend = backend;
	map->mm = request->mm;
	map->address = request->address & ~(geometry->page_size - 1U);
	map->private_slots = request->private_slots;
	map->view = NEVERC_KRT_USER_PTMAP_ORIGINAL;
	map->state_known = 1;

	result = backend->mmap_read_begin(map->mm);
	if (result)
		goto fail;
	mmap_locked = 1;
	map->mmap_held = 1;
	result = backend->mapping_validate(map->mm, map->address);
	if (result)
		goto fail;
	result = _neverc_krt_ptmap_capture_original(map);
	if (result)
		goto fail;
	/*
	 * pin_user_pages_remote(..., locked=NULL) requires mmap_lock on entry
	 * and leaves it held.  Keep the mapping snapshot protected, compare the
	 * returned struct page with vm_normal_page(original PTE), then re-read
	 * the descriptor before any lock-free snapshot or allocation work.
	 */
	result = backend->original_page_pin(
		map->mm, map->address, map->original.pfn,
		map->original_raw, &map->original.pin_token);
	if (result)
		goto fail;
	map->original.referenced = 1;
	result = _neverc_krt_ptmap_revalidate_original(map);
	if (result)
		goto fail;
	result = backend->original_page_snapshot(
		map->mm, map->address, map->original.pin_token,
		&map->original.address);
	if (result)
		goto fail;
	backend->mmap_read_end(map->mm);
	map->mmap_held = 0;
	mmap_locked = 0;
	if (!map->original.address) {
		result = -EFAULT;
		goto fail;
	}

	if (request->private_slots & NEVERC_KRT_USER_PTMAP_PRIVATE_EXEC) {
		result = _neverc_krt_ptmap_allocate_private(map, &map->exec);
		if (result)
			goto fail;
		map->exec_needs_sync = 1;
	} else {
		map->exec = map->original;
		map->exec.referenced = 0;
	}
	result = _neverc_krt_ptmap_allocate_private(map, &map->user_rw);
	if (result)
		goto fail;
	if ((map->exec.owned &&
	     (map->exec.pfn == map->original.pfn ||
	      map->exec.pfn == map->user_rw.pfn)) ||
	    map->user_rw.pfn == map->original.pfn) {
		result = -EFAULT;
		goto fail;
	}
	result = backend->mmap_read_begin(map->mm);
	if (result)
		goto fail;
	mmap_locked = 1;
	map->mmap_held = 1;
	result = _neverc_krt_ptmap_revalidate_original(map);
	if (result)
		goto fail;

	backend->mmap_read_end(map->mm);
	map->mmap_held = 0;
	mmap_locked = 0;
	if (!map->contiguous_live) {
		_neverc_krt_ptmap_free(map->contiguous_group);
		map->contiguous_group = (void *)0;
	}
	/* Commit ownership only after every fallible install step has completed.
	 * mm_count keeps the object alive without preventing mm_users from reaching
	 * zero and entering exit_mmap. */
	result = backend->mm_count_grab(map->mm);
	if (result)
		goto fail;
	map->mm_count_owned = 1;
	backend->mm_users_put(map->mm);
	__atomic_fetch_add(&_neverc_krt_ptmap_active_maps, 1U,
			   __ATOMIC_RELEASE);
	__atomic_fetch_sub(&_neverc_krt_ptmap_pending_installs, 1U,
			   __ATOMIC_ACQ_REL);
	*out_map = map;
	return 0;

fail:
	if (mmap_locked) {
		map->mmap_held = 0;
		backend->mmap_read_end(map->mm);
	}
	if (map) {
		if (map->user_rw.owned)
			backend->private_page_free(map->user_rw.pfn,
						   map->user_rw.address);
		if (map->exec.owned)
			backend->private_page_free(map->exec.pfn,
						   map->exec.address);
		if (map->original.referenced)
			backend->original_page_unpin(
				map->mm, map->address, map->original.pfn,
				map->original.pin_token, map->original.address);
		map->magic = 0;
		_neverc_krt_ptmap_free(map->contiguous_group);
		_neverc_krt_ptmap_free(map);
	}
	__atomic_fetch_sub(&_neverc_krt_ptmap_pending_installs, 1U,
			   __ATOMIC_ACQ_REL);
	return result;
}

static int _neverc_krt_ptmap_range_valid(
	const struct neverc_krt_user_ptmap *map, size_t offset, size_t size)
{
	size_t page_size = map->backend->geometry->page_size;

	return offset <= page_size && size <= page_size - offset;
}

int neverc_krt_user_ptmap_copy(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot destination,
	enum neverc_krt_user_ptmap_slot source)
{
	struct _neverc_krt_ptmap_backing *destination_backing;
	struct _neverc_krt_ptmap_backing *source_backing;
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	destination_backing = _neverc_krt_ptmap_slot(map, destination);
	source_backing = _neverc_krt_ptmap_slot(map, source);
	if (!destination_backing || !source_backing ||
	    !source_backing->address) {
		result = -EINVAL;
	} else if (!destination_backing->owned ||
		   !destination_backing->address) {
		result = -EPERM;
	} else {
		result = map->backend->nofault_read(
			destination_backing->address, source_backing->address,
			map->backend->geometry->page_size);
	}
	if (!result && destination == NEVERC_KRT_USER_PTMAP_SLOT_EXEC)
		__atomic_store_n(&map->exec_needs_sync, 1, __ATOMIC_RELEASE);
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

int neverc_krt_user_ptmap_read(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot source,
	size_t offset, void *destination, size_t size)
{
	struct _neverc_krt_ptmap_backing *source_backing;
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	if (!destination && size) {
		result = -EINVAL;
		goto out;
	}
	if (!_neverc_krt_ptmap_range_valid(map, offset, size)) {
		result = -EINVAL;
		goto out;
	}
	source_backing = _neverc_krt_ptmap_slot(map, source);
	if (!source_backing || !source_backing->address) {
		result = -EINVAL;
		goto out;
	}
	if (size)
		result = map->backend->nofault_read(
			destination,
			(const unsigned char *)source_backing->address + offset,
			size);
out:
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

int neverc_krt_user_ptmap_write(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_slot destination,
	size_t offset, const void *source, size_t size)
{
	struct _neverc_krt_ptmap_backing *destination_backing;
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	if (!source && size) {
		result = -EINVAL;
		goto out;
	}
	if (!_neverc_krt_ptmap_range_valid(map, offset, size)) {
		result = -EINVAL;
		goto out;
	}
	destination_backing = _neverc_krt_ptmap_slot(map, destination);
	if (!destination_backing) {
		result = -EINVAL;
		goto out;
	}
	if (!destination_backing->owned || !destination_backing->address) {
		result = -EPERM;
		goto out;
	}
	if (size)
		result = map->backend->nofault_write(
			(unsigned char *)destination_backing->address + offset,
			source, size);
	if (!result && destination == NEVERC_KRT_USER_PTMAP_SLOT_EXEC)
		__atomic_store_n(&map->exec_needs_sync, 1, __ATOMIC_RELEASE);
out:
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

static int _neverc_krt_ptmap_sync_exec_locked(
	struct neverc_krt_user_ptmap *map)
{
	if (!__atomic_load_n(&map->exec_needs_sync, __ATOMIC_ACQUIRE))
		return 0;
	if (!map->exec.owned || !map->exec.address)
		return -EPERM;
	map->backend->sync_exec_page(map->exec.address,
				     map->backend->geometry->page_size);
	__atomic_store_n(&map->exec_needs_sync, 0, __ATOMIC_RELEASE);
	return 0;
}

int neverc_krt_user_ptmap_sync_exec(
	struct neverc_krt_user_ptmap *map)
{
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	result = _neverc_krt_ptmap_sync_exec_locked(map);
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

static int _neverc_krt_ptmap_write_contiguous_group(
	struct neverc_krt_user_ptmap *map,
	struct _neverc_krt_ptmap_locked *locked,
	_neverc_krt_ptmap_pte_t new_descriptor, int *mutated)
{
	const _neverc_krt_ptmap_geometry_t *geometry = map->backend->geometry;
	size_t i;
	int result;

	if (!map->contiguous_live || !map->contiguous_group ||
	    locked->window.entry_count != map->contiguous_count ||
	    locked->window.target_index != map->contiguous_target ||
	    locked->window.group_address != map->contiguous_address)
		return -EAGAIN;
	for (i = 0; i < map->contiguous_count; i++) {
		if (locked->window.entries[i] != map->contiguous_group[i])
			return -EAGAIN;
	}

	for (i = 0; i < map->contiguous_count; i++) {
		result = map->backend->pte_write(&locked->window.entries[i], 0);
		if (result)
			goto restore;
		*mutated = 1;
	}
	map->backend->tlbi_range(map->contiguous_address,
				 (unsigned int)map->contiguous_count);
	for (i = 0; i < map->contiguous_count; i++) {
		_neverc_krt_ptmap_pte_t neighbor;

		if (i == map->contiguous_target)
			continue;
		neighbor = map->contiguous_group[i] &
			~geometry->contiguous_mask;
		result = map->backend->pte_write(&locked->window.entries[i],
						neighbor);
		if (result)
			goto restore;
	}
	result = map->backend->pte_write(
		&locked->window.entries[map->contiguous_target], new_descriptor);
	if (result)
		goto restore;
	map->backend->tlbi_range(map->address, 1U);
	return 0;

restore:
	if (*mutated) {
		int restored = 1;

		for (i = 0; i < map->contiguous_count; i++) {
			if (map->backend->pte_write(&locked->window.entries[i],
						    map->contiguous_group[i]))
				restored = 0;
		}
		if (restored) {
			map->backend->tlbi_range(
				map->contiguous_address,
				(unsigned int)map->contiguous_count);
			*mutated = 0;
		}
	}
	return result;
}

static int _neverc_krt_ptmap_write_single(
	struct neverc_krt_user_ptmap *map,
	struct _neverc_krt_ptmap_locked *locked,
	_neverc_krt_ptmap_pte_t old_descriptor,
	_neverc_krt_ptmap_pte_t new_descriptor, int *mutated)
{
	const _neverc_krt_ptmap_geometry_t *geometry = map->backend->geometry;
	_neverc_krt_ptmap_pte_t *entry =
		&locked->window.entries[locked->window.target_index];
	int result;

	if (old_descriptor == new_descriptor)
		return 0;
	if ((old_descriptor & geometry->descriptor_address_mask) !=
	    (new_descriptor & geometry->descriptor_address_mask)) {
		result = map->backend->pte_write(entry, 0);
		if (result)
			return result;
		*mutated = 1;
		map->backend->tlbi_range(map->address, 1U);
		result = map->backend->pte_write(entry, new_descriptor);
		if (result) {
			if (!map->backend->pte_write(entry, old_descriptor)) {
				map->backend->tlbi_range(map->address, 1U);
				*mutated = 0;
			}
			return result;
		}
		map->backend->tlbi_range(map->address, 1U);
		return 0;
	}
	result = map->backend->pte_write(entry, new_descriptor);
	if (result)
		return result;
	*mutated = 1;
	map->backend->tlbi_range(map->address, 1U);
	return 0;
}

static int _neverc_krt_ptmap_change_view_locked(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view,
	enum neverc_krt_user_ptmap_view new_view)
{
	const _neverc_krt_ptmap_geometry_t *geometry;
	struct _neverc_krt_ptmap_locked locked;
	_neverc_krt_ptmap_pte_t live_descriptor;
	_neverc_krt_ptmap_pte_t new_descriptor;
	int mutated = 0;
	int result;

	if (!_neverc_krt_ptmap_valid(map) ||
	    !_neverc_krt_ptmap_view_valid(expected_view) ||
	    !_neverc_krt_ptmap_view_valid(new_view))
		return -EINVAL;
	if (!map->state_known || map->view != expected_view)
		return -EAGAIN;
	if (new_view == NEVERC_KRT_USER_PTMAP_USER_RW_NX &&
	    !map->user_rw.owned)
		return -EPERM;
	if (new_view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY && !map->exec.address)
		return -EPERM;
	geometry = map->backend->geometry;
	new_descriptor = _neverc_krt_ptmap_descriptor_for(map, new_view);

	result = _neverc_krt_ptmap_lock(map, &locked);
	if (result)
		return result;
	live_descriptor = locked.window.entries[locked.window.target_index];
	if (live_descriptor != map->current_descriptor) {
		result = -EAGAIN;
		goto out;
	}
	if (new_view == NEVERC_KRT_USER_PTMAP_EXEC_ONLY && map->exec.owned) {
		result = _neverc_krt_ptmap_sync_exec_locked(map);
		if (result)
			goto out;
	}
	if (live_descriptor & geometry->contiguous_mask)
		result = _neverc_krt_ptmap_write_contiguous_group(
			map, &locked, new_descriptor, &mutated);
	else
		result = _neverc_krt_ptmap_write_single(
			map, &locked, live_descriptor, new_descriptor, &mutated);
	if (!result) {
		map->current_descriptor = new_descriptor;
		map->view = new_view;
		if (live_descriptor & geometry->contiguous_mask)
			map->contiguous_live = 0;
	} else if (mutated) {
		map->state_known = 0;
	}
out:
	_neverc_krt_ptmap_unlock(map, &locked);
	return result;
}

int neverc_krt_user_ptmap_transition(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view,
	enum neverc_krt_user_ptmap_view new_view)
{
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	result = _neverc_krt_ptmap_change_view_locked(
		map, expected_view, new_view);
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

int neverc_krt_user_ptmap_restore(
	struct neverc_krt_user_ptmap *map,
	enum neverc_krt_user_ptmap_view expected_view)
{
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	result = _neverc_krt_ptmap_change_view_locked(
		map, expected_view, NEVERC_KRT_USER_PTMAP_ORIGINAL);
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

static int _neverc_krt_ptmap_query_locked(
	struct neverc_krt_user_ptmap *map,
	struct neverc_krt_user_ptmap_status *status)
{
	struct _neverc_krt_ptmap_locked locked;
	_neverc_krt_ptmap_pte_t descriptor;
	int result;

	result = _neverc_krt_ptmap_lock(map, &locked);
	if (result)
		return result;
	descriptor = locked.window.entries[locked.window.target_index];
	status->address = map->address;
	status->page_size = map->backend->geometry->page_size;
	status->view = map->view;
	status->private_slots = map->private_slots;
	status->current_matches = map->state_known &&
		descriptor == map->current_descriptor;
	status->private_mapped =
		_neverc_krt_ptmap_private_pfn_mapped(map, descriptor);
	_neverc_krt_ptmap_unlock(map, &locked);
	return 0;
}

int neverc_krt_user_ptmap_query(
	struct neverc_krt_user_ptmap *map,
	struct neverc_krt_user_ptmap_status *status)
{
	int result;

	if (!status)
		return -EINVAL;
	__builtin_memset(status, 0, sizeof(*status));
	result = _neverc_krt_ptmap_operation_begin(map);
	if (result)
		return result;
	result = _neverc_krt_ptmap_query_locked(map, status);
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

int neverc_krt_user_ptmap_matches_mm(
	struct neverc_krt_user_ptmap *map, const void *borrowed_mm)
{
	int result;

	if (!borrowed_mm)
		return -EINVAL;
	result = _neverc_krt_ptmap_operation_begin(map);
	if (result)
		return result;
	result = map->mm == borrowed_mm ? 1 : 0;
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

int neverc_krt_user_ptmap_matches_current_mm(
	struct neverc_krt_user_ptmap *map)
{
	int result = _neverc_krt_ptmap_operation_begin(map);

	if (result)
		return result;
	result = map->backend->matches_current_mm(map->mm);
	_neverc_krt_ptmap_operation_end(map);
	return result;
}

int neverc_krt_user_ptmap_destroy(struct neverc_krt_user_ptmap **map_pointer)
{
	struct neverc_krt_user_ptmap_status status;
	struct neverc_krt_user_ptmap *map;
	int result;

	if (!map_pointer)
		return -EINVAL;
	map = *map_pointer;
	result = _neverc_krt_ptmap_operation_begin(map);
	if (result)
		return result;
	__builtin_memset(&status, 0, sizeof(status));
	if (_neverc_krt_ptmap_query_locked(map, &status) ||
	    !map->state_known ||
	    status.private_mapped) {
		_neverc_krt_ptmap_operation_end(map);
		return -EBUSY;
	}
	*map_pointer = (void *)0;
	_neverc_krt_ptmap_release(map);
	return 0;
}

int neverc_krt_user_fault_regs_snapshot(
	const void *opaque_pt_regs,
	struct neverc_krt_user_fault_regs *snapshot)
{
	const _neverc_krt_ptmap_backend_t *backend =
		__atomic_load_n(&_neverc_krt_ptmap_backend, __ATOMIC_ACQUIRE);
	const _neverc_krt_ptmap_geometry_t *geometry;
	struct neverc_krt_user_fault_regs value;
	const unsigned char *registers = opaque_pt_regs;
	int result;

	if (!snapshot)
		return -EINVAL;
	__builtin_memset(snapshot, 0, sizeof(*snapshot));
	if (!_neverc_krt_ptmap_backend_valid(backend) || !opaque_pt_regs)
		return -EOPNOTSUPP;
	geometry = backend->geometry;
	__builtin_memset(&value, 0, sizeof(value));
	result = backend->nofault_read(value.x, registers + geometry->pt_regs_x,
				       sizeof(value.x));
	if (result)
		return result;
	result = backend->nofault_read(&value.sp,
		registers + geometry->pt_regs_sp, sizeof(value.sp));
	if (result)
		return result;
	result = backend->nofault_read(&value.pc,
		registers + geometry->pt_regs_pc, sizeof(value.pc));
	if (result)
		return result;
	result = backend->nofault_read(&value.pstate,
		registers + geometry->pt_regs_pstate, sizeof(value.pstate));
	if (result)
		return result;
	value.user_mode =
		(value.pstate & geometry->pstate_mode_mask) ==
		geometry->pstate_user_mode;
	*snapshot = value;
	return 0;
}

int neverc_krt_user_fault_regs_commit(
	void *opaque_pt_regs,
	const struct neverc_krt_user_fault_regs *snapshot)
{
	const _neverc_krt_ptmap_backend_t *backend =
		__atomic_load_n(&_neverc_krt_ptmap_backend, __ATOMIC_ACQUIRE);
	const _neverc_krt_ptmap_geometry_t *geometry;
	unsigned char *registers = opaque_pt_regs;
	int derived_user_mode;
	int result;

	if (!_neverc_krt_ptmap_backend_valid(backend) || !opaque_pt_regs ||
	    !snapshot)
		return -EINVAL;
	geometry = backend->geometry;
	derived_user_mode =
		(snapshot->pstate & geometry->pstate_mode_mask) ==
		geometry->pstate_user_mode;
	if (!!snapshot->user_mode != derived_user_mode)
		return -EINVAL;
	result = backend->nofault_write(registers + geometry->pt_regs_x,
					snapshot->x, sizeof(snapshot->x));
	if (result)
		return result;
	result = backend->nofault_write(registers + geometry->pt_regs_sp,
					&snapshot->sp, sizeof(snapshot->sp));
	if (result)
		return result;
	result = backend->nofault_write(registers + geometry->pt_regs_pc,
					&snapshot->pc, sizeof(snapshot->pc));
	if (result)
		return result;
	return backend->nofault_write(registers + geometry->pt_regs_pstate,
				      &snapshot->pstate,
				      sizeof(snapshot->pstate));
}

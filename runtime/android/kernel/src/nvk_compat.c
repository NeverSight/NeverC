/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>
#include "nvk_internal.h"
#include "nvk_compat_table.inc"

static const struct neverc_krt_profile *_neverc_krt_selected_profile;
static struct neverc_krt_gki_layout _neverc_krt_active_effective_layout;

/*
 * Activation state.  Published once by _neverc_krt_activate_observed() during
 * single-threaded bootstrap, then read by many other functions.
 *
 * This mutable state must resolve to exactly one instance across the whole
 * module.  The embedded runtime is linked into every consumer TU, so each TU
 * carries its own copy of these globals; the final kernel `-r` link coalesces
 * them to a single instance (the final relocatable LTO resolution internalizes
 * the runtime's reserved __neverc_nvk_local.* privates; parallel codegen then
 * demotes its temporary cross-partition aliases before the object merge). They
 * are also marked externally_initialized so the -O1+ optimizer never assumes
 * the zero initializer once coalesced to one non-interposable definition.
 * Ordering is guarded by
 * _neverc_krt_selected_profile; access stays acquire/release for publish.
 */
static const struct neverc_krt_profile *_neverc_krt_active_profile;
static const struct neverc_krt_layout_entry *_neverc_krt_active_layout;
static int _neverc_krt_active_match = NEVERC_KRT_VER_UNKNOWN;

static __always_inline const struct neverc_krt_layout_entry *
_neverc_krt_find_layout(unsigned int profile_id)
{
	unsigned long i;

	for (i = 0; i < NEVERC_KRT_LAYOUT_COUNT; i++) {
		if (_neverc_krt_layouts[i].profile_id == profile_id)
			return &_neverc_krt_layouts[i];
	}
	return (const struct neverc_krt_layout_entry *)0;
}

static __always_inline int _neverc_krt_release_token_equal(
	const struct neverc_krt_layout_certificate_entry *certificate,
	const struct neverc_krt_observed_identity *identity)
{
	unsigned long i;

	if (!certificate->release_token || !identity->release_token ||
	    certificate->release_token_length != identity->release_token_length)
		return 0;
	for (i = 0; i < certificate->release_token_length; i++) {
		if (certificate->release_token[i] != identity->release_token[i])
			return 0;
	}
	return 1;
}

static __always_inline int _neverc_krt_certificate_on_variant(
	const struct neverc_krt_layout_certificate_entry *certificate,
	const struct neverc_krt_profile *profile,
	const struct neverc_krt_observed_identity *identity)
{
	return certificate->profile_id == profile->legacy_id &&
	       certificate->linux_major == identity->linux_major &&
	       certificate->linux_minor == identity->linux_minor &&
	       certificate->android_release == identity->android_release &&
	       certificate->kmi_generation == identity->kmi_generation &&
	       certificate->page_shift == identity->page_shift;
}

static __always_inline const struct neverc_krt_layout_certificate_entry *
_neverc_krt_select_layout_certificate(
	const struct neverc_krt_profile *profile,
	const struct neverc_krt_observed_identity *identity)
{
	const struct neverc_krt_layout_certificate_entry *variant = 0;
	unsigned long i;

	for (i = 0; i < NEVERC_KRT_LAYOUT_CERTIFICATE_COUNT; i++) {
		const struct neverc_krt_layout_certificate_entry *certificate =
			&_neverc_krt_layout_certificates[i];

		if (!_neverc_krt_certificate_on_variant(certificate, profile,
							identity))
			continue;
		if (_neverc_krt_release_token_equal(certificate, identity))
			return certificate;
		if (!variant)
			variant = certificate;
	}
	return variant;
}

static __always_inline unsigned long _neverc_krt_match_layout_certificates(
	const struct neverc_krt_profile *profile,
	const struct neverc_krt_layout_entry *layout,
	const struct neverc_krt_observed_identity *identity,
	struct neverc_krt_gki_layout *effective_layout)
{
	const struct neverc_krt_layout_certificate_entry *certificate;
	unsigned long field_bits = 0;
	unsigned long matched_bits;

	if (!profile || !layout || !identity || !identity->has_android_identity)
		return 0;
	/*
	 * Prefer a byte-for-byte release token.  Otherwise accept any
	 * certificate for this selected series plus the live Android/KMI
	 * pair so a selected family can overlay another Android
	 * generation on the same Linux series.
	 */
	certificate = _neverc_krt_select_layout_certificate(profile, identity);
	if (!certificate)
		return 0;

	matched_bits = certificate->field_bits;
	/* dir_context proves equality with the family layout.  The other
	 * certificates carry live-identity offsets that overlay it. */
	if ((matched_bits & NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT) &&
	    (certificate->dir_context_size !=
			layout->layout.dir_context_size ||
	     certificate->dir_context_actor !=
			layout->layout.dir_context_actor ||
	     certificate->dir_context_actor_size !=
			layout->layout.dir_context_actor_size ||
	     certificate->dir_context_pos !=
			layout->layout.dir_context_pos ||
	     certificate->dir_context_pos_size !=
			layout->layout.dir_context_pos_size ||
	     certificate->filldir_abi != profile->caps.filldir_abi))
		matched_bits &= ~NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT;

	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME) {
		effective_layout->filename_size = certificate->filename_size;
		effective_layout->filename_name = certificate->filename_name;
		effective_layout->filename_name_size =
			certificate->filename_name_size;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_PATH_INODE) {
		effective_layout->path_size = certificate->path_size;
		effective_layout->path_dentry = certificate->path_dentry;
		effective_layout->path_dentry_size =
			certificate->path_dentry_size;
		effective_layout->dentry_size = certificate->dentry_size;
		effective_layout->dentry_inode = certificate->dentry_inode;
		effective_layout->dentry_inode_size =
			certificate->dentry_inode_size;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_INODE_TIMES) {
		effective_layout->inode_size = certificate->inode_size;
		effective_layout->inode_atime_sec =
			certificate->inode_atime_sec;
		effective_layout->inode_atime_sec_size =
			certificate->inode_atime_sec_size;
		effective_layout->inode_mtime_sec =
			certificate->inode_mtime_sec;
		effective_layout->inode_mtime_sec_size =
			certificate->inode_mtime_sec_size;
		effective_layout->inode_atime_nsec =
			certificate->inode_atime_nsec;
		effective_layout->inode_atime_nsec_size =
			certificate->inode_atime_nsec_size;
		effective_layout->inode_mtime_nsec =
			certificate->inode_mtime_nsec;
		effective_layout->inode_mtime_nsec_size =
			certificate->inode_mtime_nsec_size;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_TASK_WALK) {
		effective_layout->task_size =
			certificate->task_walk_task_size;
		effective_layout->task_tasks = certificate->task_tasks;
		effective_layout->task_mm = certificate->task_mm;
		effective_layout->task_parent = certificate->task_parent;
		effective_layout->task_real_parent =
			certificate->task_real_parent;
		effective_layout->task_group_leader =
			certificate->task_group_leader;
		effective_layout->task_real_cred =
			certificate->task_real_cred;
		effective_layout->task_comm = certificate->task_comm;
		effective_layout->cred_size = certificate->cred_size;
		effective_layout->cred_uid = certificate->cred_uid;
		effective_layout->cred_gid = certificate->cred_gid;
		effective_layout->cred_suid = certificate->cred_suid;
		effective_layout->cred_sgid = certificate->cred_sgid;
		effective_layout->cred_euid = certificate->cred_euid;
		effective_layout->cred_egid = certificate->cred_egid;
		effective_layout->cred_fsuid = certificate->cred_fsuid;
		effective_layout->cred_fsgid = certificate->cred_fsgid;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_TASK_REF) {
		effective_layout->task_size =
			certificate->task_ref_task_size;
		effective_layout->task_usage = certificate->task_usage;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE) {
		effective_layout->task_size =
			certificate->task_user_state_task_size;
		effective_layout->task_stack = certificate->task_stack;
		effective_layout->task_stack_refcount =
			certificate->task_stack_refcount;
		effective_layout->task_flags = certificate->task_flags;
		effective_layout->pt_regs_size = certificate->pt_regs_size;
		effective_layout->pt_regs_pc = certificate->pt_regs_pc;
		effective_layout->pt_regs_pstate =
			certificate->pt_regs_pstate;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_TASK_THREADS) {
		effective_layout->task_size = certificate->task_size;
		effective_layout->task_pid = certificate->task_pid;
		effective_layout->task_thread_pid =
			certificate->task_thread_pid;
		effective_layout->task_signal = certificate->task_signal;
		effective_layout->task_thread_node =
			certificate->task_thread_node;
		effective_layout->signal_size = certificate->signal_size;
		effective_layout->signal_thread_head =
			certificate->signal_thread_head;
	}
	if (matched_bits & NEVERC_KRT_LAYOUT_CERT_USER_PTMAP) {
		effective_layout->mm_size = certificate->mm_size;
		effective_layout->mm_count = certificate->mm_count;
		effective_layout->mm_count_size =
			certificate->mm_count_size;
		effective_layout->mm_pgd = certificate->mm_pgd;
		effective_layout->mm_pgd_size = certificate->mm_pgd_size;
		effective_layout->mm_page_table_lock =
			certificate->mm_page_table_lock;
		effective_layout->mm_page_table_lock_size =
			certificate->mm_page_table_lock_size;
		effective_layout->mm_mmap_lock =
			certificate->mm_mmap_lock;
		effective_layout->mm_mmap_lock_size =
			certificate->mm_mmap_lock_size;
		effective_layout->vma_size = certificate->vma_size;
		effective_layout->vma_start = certificate->vma_start;
		effective_layout->vma_start_size =
			certificate->vma_start_size;
		effective_layout->vma_end = certificate->vma_end;
		effective_layout->vma_end_size =
			certificate->vma_end_size;
		if (certificate->vma_mm_size &&
		    certificate->vma_flags_size &&
		    certificate->vma_pgoff_size) {
			effective_layout->vma_mm = certificate->vma_mm;
			effective_layout->vma_flags =
				certificate->vma_flags;
			effective_layout->vma_pgoff =
				certificate->vma_pgoff;
		}
		effective_layout->pt_regs_size =
			certificate->pt_regs_size;
		effective_layout->pt_regs_regs =
			certificate->pt_regs_regs;
		effective_layout->pt_regs_regs_size =
			certificate->pt_regs_regs_size;
		effective_layout->pt_regs_sp = certificate->pt_regs_sp;
		effective_layout->pt_regs_sp_size =
			certificate->pt_regs_sp_size;
		effective_layout->pt_regs_pc = certificate->pt_regs_pc;
		effective_layout->pt_regs_pc_size =
			certificate->pt_regs_pc_size;
		effective_layout->pt_regs_pstate =
			certificate->pt_regs_pstate;
		effective_layout->pt_regs_pstate_size =
			certificate->pt_regs_pstate_size;
		effective_layout->user_page_shift =
			certificate->user_page_shift;
		effective_layout->user_va_bits =
			certificate->user_va_bits;
		effective_layout->user_pa_bits =
			certificate->user_pa_bits;
		effective_layout->user_pgtable_levels =
			certificate->user_pgtable_levels;
		effective_layout->user_pgd_shift =
			certificate->user_pgd_shift;
		effective_layout->user_pmd_shift =
			certificate->user_pmd_shift;
		effective_layout->user_pte_shift =
			certificate->user_pte_shift;
		effective_layout->user_index_bits =
			certificate->user_index_bits;
		effective_layout->user_contiguous_bit =
			certificate->user_contiguous_bit;
		effective_layout->user_contiguous_entries =
			certificate->user_contiguous_entries;
		effective_layout->user_descriptor_address_mask =
			certificate->user_descriptor_address_mask;
		effective_layout->user_physical_address_mask =
			certificate->user_physical_address_mask;
		effective_layout->user_physical_page_mask =
			certificate->user_physical_page_mask;
		effective_layout->user_tlbi_all_asid =
			certificate->user_tlbi_all_asid;
	}
	field_bits |= matched_bits & NEVERC_KRT_LAYOUT_CERT_PRIVATE_FIELDS;
	return field_bits;
}

static __always_inline const struct neverc_krt_profile *
_neverc_krt_current_profile(void)
{
	return __atomic_load_n(&_neverc_krt_active_profile, __ATOMIC_ACQUIRE);
}

static __always_inline const struct neverc_krt_layout_entry *
_neverc_krt_current_layout(void)
{
	if (!_neverc_krt_current_profile())
		return (const struct neverc_krt_layout_entry *)0;
	return __atomic_load_n(&_neverc_krt_active_layout, __ATOMIC_ACQUIRE);
}

int _neverc_krt_version_setup(unsigned int profile_id)
{
	const struct neverc_krt_profile *profile =
		neverc_krt_find_profile(profile_id);
	const struct neverc_krt_profile *selected;

	if (!profile || !_neverc_krt_find_layout(profile->legacy_id))
		return -1;
	selected = __atomic_load_n(&_neverc_krt_selected_profile,
				   __ATOMIC_ACQUIRE);
	if (selected)
		return selected->legacy_id == profile_id ? 0 : -2;
	if (!__atomic_compare_exchange_n(&_neverc_krt_selected_profile, &selected,
					 profile, 0, __ATOMIC_RELEASE,
					 __ATOMIC_ACQUIRE))
		return selected->legacy_id == profile_id ? 0 : -2;
	return 0;
}

const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void)
{
	const struct neverc_krt_profile *profile = _neverc_krt_current_profile();

	return profile ? &profile->caps :
		(const struct neverc_krt_runtime_caps *)0;
}

int _neverc_krt_current_kcfi_mode(void)
{
	const struct neverc_krt_profile *profile = _neverc_krt_current_profile();

	return profile ? (int)profile->kcfi_mode : -1;
}

int _neverc_krt_current_profile_id(void)
{
	const struct neverc_krt_profile *profile = _neverc_krt_current_profile();

	return profile ? (int)profile->legacy_id : -1;
}

unsigned long _neverc_krt_get_module_size(void)
{
	const struct neverc_krt_layout_entry *layout =
		_neverc_krt_current_layout();

	return layout ? layout->module_size : 0;
}

unsigned long _neverc_krt_get_kimage_vaddr_base(void)
{
	const struct neverc_krt_profile *profile = _neverc_krt_current_profile();

	return profile ? profile->kimage_vaddr : 0;
}

const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void)
{
	return _neverc_krt_current_layout() ?
		&_neverc_krt_active_effective_layout :
		(const struct neverc_krt_gki_layout *)0;
}

unsigned long _neverc_krt_get_file_dentry_off(void)
{
	const struct neverc_krt_layout_entry *layout =
		_neverc_krt_current_layout();

	return layout ? layout->file_dentry_off : 0;
}

/* ---- internal variables ---- */

static struct neverc_krt_kernel_info _neverc_krt_kinfo;

typedef int (*neverc_krt_fmt_write_fn)(char *buf, size_t size,
				       const char *fmt, __builtin_va_list ap);
typedef int (*neverc_krt_fmt_read_fn)(const char *buf, const char *fmt,
				      __builtin_va_list ap);
static neverc_krt_fmt_write_fn _neverc_krt_fmt_slot_0;
static neverc_krt_fmt_read_fn  _neverc_krt_fmt_slot_1;

static unsigned long _neverc_krt_rt_off_init;
static unsigned long _neverc_krt_rt_off_exit;

static __always_inline unsigned int _neverc_krt_runtime_page_shift(void)
{
	unsigned long tcr;

	__asm__ __volatile__("mrs %0, tcr_el1" : "=r"(tcr));
	switch ((tcr >> 30) & 3UL) {
	case 1:
		return 14; /* 16 KiB */
	case 2:
		return 12; /* 4 KiB */
	case 3:
		return 16; /* 64 KiB */
	default:
		return 0;
	}
}

static int
_neverc_krt_activate_observed(
	const struct neverc_krt_kernel_info *observed,
	const struct neverc_krt_observed_identity *identity)
{
	const struct neverc_krt_profile *profile;
	const struct neverc_krt_profile *selected;
	const struct neverc_krt_profile *active;
	const struct neverc_krt_layout_entry *layout;
	enum neverc_krt_profile_match profile_match;
	int version_match;

	selected = __atomic_load_n(&_neverc_krt_selected_profile,
				   __ATOMIC_ACQUIRE);
	if (selected) {
		profile = selected;
		profile_match = neverc_krt_match_profile(profile, identity);
		if (profile_match == NEVERC_KRT_PROFILE_MATCH_NONE)
			return -1;
	} else {
		/* Auto-selection is intentionally exact-only.  A major.minor
		 * compatibility decision needs the profile pinned by the build. */
		profile = neverc_krt_find_profile_by_identity(
			identity->linux_major, identity->linux_minor,
			identity->linux_patch, identity->android_release,
			identity->kmi_generation, identity->page_shift,
			identity->release_token, identity->release_token_length);
		if (!profile)
			return -1;
		profile_match = NEVERC_KRT_PROFILE_MATCH_EXACT;
		if (!__atomic_compare_exchange_n(
				&_neverc_krt_selected_profile, &selected, profile, 0,
				__ATOMIC_RELEASE, __ATOMIC_ACQUIRE) &&
		    selected->legacy_id != profile->legacy_id)
			return -2;
	}
	layout = _neverc_krt_find_layout(profile->legacy_id);
	if (!layout)
		return -1;
	__builtin_memcpy(&_neverc_krt_active_effective_layout, &layout->layout,
			 sizeof(_neverc_krt_active_effective_layout));
	version_match = profile_match == NEVERC_KRT_PROFILE_MATCH_EXACT ?
		NEVERC_KRT_VER_EXACT : NEVERC_KRT_VER_COMPAT;
	if (version_match == NEVERC_KRT_VER_COMPAT) {
		unsigned long certificate_bits =
			_neverc_krt_match_layout_certificates(
				profile, layout, identity,
				&_neverc_krt_active_effective_layout);

		/*
		 * 6.x: same Linux series, different Android/KMI than the
		 * selected compile family — every private layout field must
		 * come from a leftover certificate.  5.x COMPAT only checks
		 * series + Android generation + page (match_profile already
		 * rejected a wrong 12/13/14); KMI/patch/token differences
		 * keep the family layout without a certificate.
		 */
		if (profile->linux_major >= 6U &&
		    identity->has_android_identity &&
		    (identity->android_release != profile->android_release ||
		     identity->kmi_generation != profile->kmi_generation) &&
		    (certificate_bits & NEVERC_KRT_LAYOUT_CERT_PRIVATE_FIELDS) !=
			    NEVERC_KRT_LAYOUT_CERT_PRIVATE_FIELDS)
			return -1;
	}

	_neverc_krt_kinfo = *observed;
	__atomic_store_n(&_neverc_krt_active_layout, layout, __ATOMIC_RELEASE);
	__atomic_store_n(&_neverc_krt_active_match, version_match,
			 __ATOMIC_RELEASE);
	/*
	 * Publish the active profile with an acq_rel compare-exchange (the
	 * format-slot idiom).  Activation is single threaded (guarded by
	 * _neverc_krt_selected_profile).
	 */
	active = (const struct neverc_krt_profile *)0;
	if (!__atomic_compare_exchange_n(&_neverc_krt_active_profile, &active,
					 profile, 0, __ATOMIC_ACQ_REL,
					 __ATOMIC_ACQUIRE) &&
	    active->legacy_id != profile->legacy_id)
		return -2;
	return 0;
}

static int _neverc_krt_version_try_detect_from_banner(void)
{
	const char *banner;
	char buf[128];
	struct neverc_krt_observed_identity identity;
	struct neverc_krt_kernel_info observed = {0};

	if (_neverc_krt_current_profile())
		return 0;
	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner)
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	if (!banner)
		return -1;
	if (neverc_krt_mem_read(buf, banner, sizeof(buf)) != 0)
		return -1;
	buf[sizeof(buf) - 1] = '\0';
	if (neverc_krt_parse_banner_identity(buf, &identity))
		return -1;

	observed.major = identity.linux_major;
	observed.minor = identity.linux_minor;
	observed.patch = identity.linux_patch;
	observed.android_version = identity.android_release;
	observed.kmi_generation = identity.kmi_generation;
	observed.page_shift = _neverc_krt_runtime_page_shift();
	observed.detected = 1;
	identity.page_shift = observed.page_shift;
	_neverc_krt_kinfo = observed;
	return _neverc_krt_activate_observed(&observed, &identity);
}

int neverc_krt_compat_init(void)
{
	int ret;

	if (_neverc_krt_current_profile())
		return 0;
	ret = neverc_krt_mem_init();
	if (ret)
		return ret;
	ret = _neverc_krt_version_try_detect_from_banner();
	if (ret)
		return ret;
	(void)neverc_krt_fmt_init();
	return 0;
}

int neverc_krt_fmt_init(void)
{
	neverc_krt_fmt_write_fn write;
	neverc_krt_fmt_read_fn read;

	write = __atomic_load_n(&_neverc_krt_fmt_slot_0, __ATOMIC_ACQUIRE);
	if (!write) {
		neverc_krt_fmt_write_fn resolved =
			(neverc_krt_fmt_write_fn)NEVERC_KRT_LOOKUP("vsnprintf");
		if (resolved && !__atomic_compare_exchange_n(
				&_neverc_krt_fmt_slot_0, &write, resolved, 0,
				__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			resolved = write;
		write = resolved ? resolved :
			__atomic_load_n(&_neverc_krt_fmt_slot_0,
					__ATOMIC_ACQUIRE);
	}
	read = __atomic_load_n(&_neverc_krt_fmt_slot_1, __ATOMIC_ACQUIRE);
	if (!read) {
		neverc_krt_fmt_read_fn resolved =
			(neverc_krt_fmt_read_fn)NEVERC_KRT_LOOKUP("vsscanf");
		if (resolved && !__atomic_compare_exchange_n(
				&_neverc_krt_fmt_slot_1, &read, resolved, 0,
				__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			resolved = read;
		read = resolved;
	}
	return write ? 0 : -1;
}

int neverc_krt_snprintf(char *buf, size_t size, const char *fmt, ...)
{
	neverc_krt_fmt_write_fn write =
		__atomic_load_n(&_neverc_krt_fmt_slot_0, __ATOMIC_ACQUIRE);
	if (!write) return -1;
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	int ret = write(buf, size, fmt, ap);
	__builtin_va_end(ap);
	return ret;
}

int neverc_krt_sscanf(const char *buf, const char *fmt, ...)
{
	neverc_krt_fmt_read_fn read =
		__atomic_load_n(&_neverc_krt_fmt_slot_1, __ATOMIC_ACQUIRE);
	if (!read) return -1;
	__builtin_va_list ap;
	__builtin_va_start(ap, fmt);
	int ret = read(buf, fmt, ap);
	__builtin_va_end(ap);
	return ret;
}

int neverc_krt_has_cfi(void)
{
	return NEVERC_KRT_LOOKUP("__cfi_check") != (void *)0;
}

int neverc_krt_check_kernel_match(void)
{
	int ret = neverc_krt_compat_init();

	if (!ret && _neverc_krt_current_profile())
		return __atomic_load_n(&_neverc_krt_active_match,
				       __ATOMIC_ACQUIRE);
	return _neverc_krt_kinfo.detected ? NEVERC_KRT_VER_MISMATCH :
		NEVERC_KRT_VER_UNKNOWN;
}

int neverc_krt_verify_module_offsets(struct neverc_krt_this_module *mod,
				     const char *expected_name)
{
	const struct neverc_krt_gki_layout *layout =
		_neverc_krt_get_gki_layout();
	struct list_head *list;
	const char *name;

	if (!layout || !mod)
		return -1;
	list = (struct list_head *)((char *)mod + layout->module_list);
	{
		unsigned long ln, lp;
		if (neverc_krt_mem_read(&ln, &list->next, 8))
			return -1;
		if (neverc_krt_mem_read(&lp, &list->prev, 8))
			return -2;
		if (ln < 0xFFFF000000000000UL &&
		    ln != (unsigned long)list)
			return -1;
		if (lp < 0xFFFF000000000000UL &&
		    lp != (unsigned long)list)
			return -2;
	}

	name = (const char *)((char *)mod + layout->module_name);
	if (expected_name) {
		int elen = 0;
		while (expected_name[elen]) elen++;
		char nbuf[64];
		if (elen >= (int)sizeof(nbuf))
			elen = (int)sizeof(nbuf) - 1;
		if (neverc_krt_mem_read(nbuf, name, elen + 1))
			return -3;
		nbuf[elen] = '\0';
		const char *a = nbuf;
		const char *b = expected_name;
		while (*a && *b && *a == *b) { a++; b++; }
		if (*a != *b) return -3;
	} else {
		unsigned char c;
		if (neverc_krt_mem_read(&c, name, 1))
			return -4;
		if (c < 0x20 || c > 0x7E)
			return -5;
	}

	return 0;
}

int neverc_krt_probe_module_offsets(struct neverc_krt_this_module *mod,
				    void *expected_init,
				    void *expected_exit)
{
	if (!mod || !expected_init) return -1;

	const unsigned char *base = (const unsigned char *)mod;
	unsigned long i;

	for (i = 64; i < _neverc_krt_get_module_size(); i += 8) {
		unsigned long v;
		if (neverc_krt_mem_read(&v, base + i, 8)) continue;
		if (v == (unsigned long)expected_init) {
			_neverc_krt_rt_off_init = i;
			break;
		}
	}

	if (expected_exit && _neverc_krt_rt_off_init) {
		for (i = _neverc_krt_rt_off_init + 8; i < _neverc_krt_get_module_size(); i += 8) {
			unsigned long v;
			if (neverc_krt_mem_read(&v, base + i, 8)) continue;
			if (v == (unsigned long)expected_exit) {
				_neverc_krt_rt_off_exit = i;
				break;
			}
		}
	}

	return _neverc_krt_rt_off_init ? 0 : -1;
}

int neverc_krt_validate_runtime(struct neverc_krt_this_module *mod,
				const char *name,
				void *init_fn, void *exit_fn)
{
	int ret;

	ret = neverc_krt_check_kernel_match();
	if (ret < 0)
		return ret;
	(void)init_fn;
	(void)exit_fn;
	return neverc_krt_verify_module_offsets(mod, name);
}

int neverc_krt_patch_vermagic(struct neverc_krt_this_module *mod)
{
	const char *banner;
	char ban_raw[128];
	char ver_buf[128];
	unsigned char *base;
	unsigned long modsz;
	unsigned long written;
	unsigned long scan;
	int match = neverc_krt_check_kernel_match();
	int formatted;

	if (match < 0)
		return match;

	/*
	 * This rewrites a "vermagic=" blob inside this_module after the
	 * selected identity has already been accepted.  The loader compared
	 * .modinfo before init, so this cannot make insmod accept another
	 * kernel.
	 */
	banner = (const char *)NEVERC_KRT_LOOKUP("linux_banner");
	if (!banner)
		banner = (const char *)NEVERC_KRT_LOOKUP("linux_proc_banner");
	if (!banner)
		return -1;

	if (neverc_krt_mem_read(ban_raw, banner, sizeof(ban_raw)))
		return -1;
	ban_raw[sizeof(ban_raw) - 1] = '\0';
	formatted = neverc_krt_format_vermagic_from_banner(
		ban_raw, ver_buf, sizeof(ver_buf));
	if (formatted)
		return formatted;

	written = 0;
	while (ver_buf[written])
		written++;

	base = (unsigned char *)mod;
	modsz = _neverc_krt_get_module_size();
	for (scan = 0; scan + 9 < modsz; scan++) {
		unsigned char sw[9];

		if (neverc_krt_mem_read(sw, base + scan, 9))
			continue;
		if (sw[0] == 'v' && sw[1] == 'e' &&
		    sw[2] == 'r' && sw[3] == 'm' &&
		    sw[4] == 'a' && sw[5] == 'g' &&
		    sw[6] == 'i' && sw[7] == 'c' && sw[8] == '=') {
			neverc_krt_mem_write_protected(
				(unsigned long)(base + scan + 9),
				ver_buf, written + 1);
			return 0;
		}
	}

	return -3;
}

int neverc_krt_fixup_runtime(struct neverc_krt_this_module *mod,
			     const char *name,
			     void *init_fn, void *exit_fn)
{
	int ret;

	ret = neverc_krt_check_kernel_match();
	if (ret < 0)
		return ret;
	(void)init_fn;
	(void)exit_fn;
	return neverc_krt_verify_module_offsets(mod, name);
}

void *neverc_krt_lookup_printk(void)
{
	void *sym = NEVERC_KRT_LOOKUP("_printk");
	if (!sym) sym = NEVERC_KRT_LOOKUP("printk");
	return sym;
}

void *neverc_krt_lookup_copy_from_user(void)
{
	neverc_krt_mem_init();
	return (void *)_neverc_krt_mem_copy_from_user_compat;
}

void *neverc_krt_lookup_copy_to_user(void)
{
	neverc_krt_mem_init();
	return (void *)_neverc_krt_mem_copy_to_user_compat;
}

void *neverc_krt_lookup_probe_read(void)
{
	void *sym = NEVERC_KRT_LOOKUP("copy_from_kernel_nofault");
	if (!sym) sym = NEVERC_KRT_LOOKUP("probe_kernel_read");
	return sym;
}

void *neverc_krt_lookup_probe_write(void)
{
	void *sym = NEVERC_KRT_LOOKUP("copy_to_kernel_nofault");
	if (!sym) sym = NEVERC_KRT_LOOKUP("probe_kernel_write");
	return sym;
}

void *neverc_krt_lookup_module_alloc(void)
{
	void *sym = NEVERC_KRT_LOOKUP("execmem_alloc");
	if (!sym) sym = NEVERC_KRT_LOOKUP("module_alloc");
	return sym;
}

void *neverc_krt_lookup_module_free(void)
{
	void *sym = NEVERC_KRT_LOOKUP("execmem_free");
	if (!sym) sym = NEVERC_KRT_LOOKUP("module_memfree");
	if (!sym) sym = NEVERC_KRT_LOOKUP("vfree");
	return sym;
}

const struct neverc_krt_kernel_info *neverc_krt_kernel_version(void)
{
	neverc_krt_compat_init();
	return &_neverc_krt_kinfo;
}

u32 neverc_krt_kernel_code(void)
{
	neverc_krt_compat_init();
	return _neverc_krt_kinfo.major * 10000 + _neverc_krt_kinfo.minor * 100
	       + _neverc_krt_kinfo.patch;
}

int neverc_krt_kernel_ge(u32 maj, u32 min)
{
	neverc_krt_compat_init();
	return _neverc_krt_kinfo.major > maj ||
	       (_neverc_krt_kinfo.major == maj && _neverc_krt_kinfo.minor >= min);
}

int neverc_krt_kernel_lt(u32 maj, u32 min)
{
	return !neverc_krt_kernel_ge(maj, min);
}

int neverc_krt_should_abort_on_mismatch(void)
{
	int r = neverc_krt_check_kernel_match();
	return r < 0;
}

unsigned long neverc_krt_rt_off_init(void)
{
	const struct neverc_krt_layout_entry *layout;

	if (_neverc_krt_rt_off_init) return _neverc_krt_rt_off_init;
	layout = _neverc_krt_current_layout();
	return layout ? layout->off_init : 0;
}

unsigned long neverc_krt_rt_off_exit(void)
{
	const struct neverc_krt_layout_entry *layout;

	if (_neverc_krt_rt_off_exit) return _neverc_krt_rt_off_exit;
	layout = _neverc_krt_current_layout();
	return layout ? layout->off_exit : 0;
}

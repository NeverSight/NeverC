/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_internal.h — Cross-file internal declarations for NVK runtime.
 *
 * This header is only included by runtime C sources. User code must not
 * include it.
 */
#ifndef NEVERC_KRT_INTERNAL_H
#define NEVERC_KRT_INTERNAL_H

#include "nvk_profile.h"

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <nvk_interpose_advanced.h>

/* ---- ARM64 instruction constants (used by nvk_interpose.c, nvk_anti.c) ---- */

#define NEVERC_KRT_A64_NOP       0xD503201FU
#define NEVERC_KRT_A64_BTI_C     0xD503245FU
#define NEVERC_KRT_A64_BTI_JC    0xD50324DFU
#define NEVERC_KRT_A64_PACIASP   0xD503233FU
#define NEVERC_KRT_A64_PACIBSP   0xD503237FU
#define NEVERC_KRT_A64_AUTIASP   0xD50323BFU
#define NEVERC_KRT_A64_MOV_X0_0  0xD2800000U
#define NEVERC_KRT_A64_MOV_X0_XZR 0xAA1F03E0U
#define NEVERC_KRT_A64_RET       0xD65F03C0U
#define NEVERC_KRT_A64_LDR_X16_PC8 0x58000050U
#define NEVERC_KRT_A64_BR_X16    0xD61F0200U
#define NEVERC_KRT_A64_BR_X17    0xD61F0220U

#define NEVERC_KRT_A64_RET_X16   0xD65F0200U
#define NEVERC_KRT_A64_RET_X17   0xD65F0220U

/*
 * Use BR Xn for transfers onto unsigned execmem: RET Xn authenticates PAC and
 * soft-fails those trampoline targets to a bad PC.  Mid-function continuations
 * and PAC-signed caller LRs keep RET so BTI treats the transfer as a return
 * (BTYPE=00), while replacement calls use BLR.  Strip PAC before publishing a
 * direct force_jump target.
 */

/* ---- Interpose engine constants (used by nvk_interpose.c, nvk_anti.c) ---- */

#define NEVERC_KRT_INTERPOSE_TRAMP_CAP  64
#define NEVERC_KRT_INTERPOSE_STUB_CAP  128

/* ---- Shared typedefs (used across multiple .c files) ---- */

typedef int  (*neverc_krt_pte_rw_fn)(unsigned long addr);
struct mm_struct;
typedef struct mm_struct *(*neverc_krt_get_task_mm_fn)(
	struct task_struct *task);
typedef void (*neverc_krt_mmput_fn)(struct mm_struct *mm);
typedef unsigned long (*_neverc_krt_sym_resolver_fn)(const char *name);

/*
 * Configured GKI offsets consumed by runtime code.  These are generated from
 * the per-profile BTF/DWARF manifests; no C source may guess them from a
 * kernel-version range or a maximum-sized opaque structure.
 */
struct neverc_krt_gki_layout {
	unsigned long task_size;
	unsigned long task_tasks;
	unsigned long task_usage;
	unsigned long task_stack;
	unsigned long task_stack_refcount;
	unsigned long task_stack_size;
	unsigned long task_flags;
	unsigned long task_mm;
	unsigned long task_pid;
	unsigned long task_tgid;
	unsigned long task_parent;
	unsigned long task_real_parent;
	unsigned long task_thread_pid;
	unsigned long task_signal;
	unsigned long task_thread_node;
	unsigned long task_group_leader;
	unsigned long task_real_cred;
	unsigned long task_cred;
	unsigned long task_comm;
	unsigned long task_comm_size;
	unsigned long task_nsproxy;
	unsigned long task_seccomp;
	unsigned long signal_size;
	unsigned long signal_thread_head;
	unsigned long pt_regs_size;
	unsigned long pt_regs_regs;
	unsigned long pt_regs_regs_size;
	unsigned long pt_regs_sp;
	unsigned long pt_regs_sp_size;
	unsigned long pt_regs_pc;
	unsigned long pt_regs_pc_size;
	unsigned long pt_regs_pstate;
	unsigned long pt_regs_pstate_size;
	unsigned long cred_size;
	unsigned long cred_uid;
	unsigned long cred_gid;
	unsigned long cred_suid;
	unsigned long cred_sgid;
	unsigned long cred_euid;
	unsigned long cred_egid;
	unsigned long cred_fsuid;
	unsigned long cred_fsgid;
	unsigned long cred_securebits;
	unsigned long cred_cap_inheritable;
	unsigned long cred_cap_permitted;
	unsigned long cred_cap_effective;
	unsigned long cred_cap_bset;
	unsigned long cred_cap_ambient;
	unsigned long mm_size;
	unsigned long mm_count;
	unsigned long mm_count_size;
	unsigned long mm_pgd;
	unsigned long mm_pgd_size;
	unsigned long mm_page_table_lock;
	unsigned long mm_page_table_lock_size;
	unsigned long mm_mmap_lock;
	unsigned long mm_mmap_lock_size;
	unsigned long vma_size;
	unsigned long vma_start;
	unsigned long vma_start_size;
	unsigned long vma_end;
	unsigned long vma_end_size;
	unsigned long vma_mm;
	unsigned long vma_page_prot;
	unsigned long vma_flags;
	unsigned long vma_pgoff;
	unsigned long vma_file;
	unsigned long dir_context_size;
	unsigned long dir_context_actor;
	unsigned long dir_context_actor_size;
	unsigned long dir_context_pos;
	unsigned long dir_context_pos_size;
	unsigned long vmap_va_start;
	unsigned long vmap_va_end;
	unsigned long module_list;
	unsigned long module_name;
	unsigned long module_kobj;
	unsigned long kobject_name;
	unsigned long skb_data;
	unsigned long sock_dport;
	unsigned long sock_num;
	unsigned long nsproxy_mnt_ns;
	unsigned long nsproxy_net_ns;
	unsigned long seccomp_mode;
	unsigned long kstat_size;
	unsigned long kstat_mode;
	unsigned long kstat_uid;
	unsigned long kstat_gid;
	unsigned long kstat_file_size;
	unsigned long dentry_name;
	unsigned long file_dentry;
	unsigned long module_size;
	unsigned long module_init;
	unsigned long module_exit;
	unsigned long ftrace_ops_func;
	unsigned long ftrace_ops_flags;
	unsigned long filename_size;
	unsigned long filename_name;
	unsigned long filename_name_size;
	unsigned long path_size;
	unsigned long path_dentry;
	unsigned long path_dentry_size;
	unsigned long dentry_size;
	unsigned long dentry_inode;
	unsigned long dentry_inode_size;
	unsigned long inode_size;
	unsigned long inode_atime_sec;
	unsigned long inode_atime_sec_size;
	unsigned long inode_mtime_sec;
	unsigned long inode_mtime_sec_size;
	unsigned long inode_atime_nsec;
	unsigned long inode_atime_nsec_size;
	unsigned long inode_mtime_nsec;
	unsigned long inode_mtime_nsec_size;
	unsigned long user_page_shift;
	unsigned long user_va_bits;
	unsigned long user_pa_bits;
	unsigned long user_pgtable_levels;
	unsigned long user_pgd_shift;
	unsigned long user_pmd_shift;
	unsigned long user_pte_shift;
	unsigned long user_index_bits;
	unsigned long user_contiguous_bit;
	unsigned long user_contiguous_entries;
	unsigned long user_descriptor_address_mask;
	unsigned long user_physical_address_mask;
	unsigned long user_physical_page_mask;
	unsigned long user_tlbi_all_asid;
};

/*
 * Page-aligned physical address mask for the live TCR granule.
 * PA width comes from the active layout when present; the mask itself
 * is always computed from that width plus the live shift so a 4K
 * catalog cannot paint a 16K walk.
 */
unsigned long _neverc_krt_physical_page_mask_for_shift(
	const struct neverc_krt_gki_layout *layout, int page_shift);

/* Exact per-field evidence published only for a certified live identity. */
#define NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT (1UL << 0)
#define NEVERC_KRT_LAYOUT_CERT_INODE_TIMES (1UL << 1)
#define NEVERC_KRT_LAYOUT_CERT_PATH_INODE  (1UL << 2)
#define NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME (1UL << 3)
#define NEVERC_KRT_LAYOUT_CERT_TASK_THREADS (1UL << 4)
#define NEVERC_KRT_LAYOUT_CERT_TASK_WALK (1UL << 5)
#define NEVERC_KRT_LAYOUT_CERT_TASK_REF (1UL << 6)
#define NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE (1UL << 7)
#define NEVERC_KRT_LAYOUT_CERT_USER_PTMAP (1UL << 8)
#define NEVERC_KRT_LAYOUT_CERT_FILE_DENTRY (1UL << 9)
#define NEVERC_KRT_LAYOUT_CERT_PRIVATE_FIELDS \
	(NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT | \
	 NEVERC_KRT_LAYOUT_CERT_INODE_TIMES | \
	 NEVERC_KRT_LAYOUT_CERT_PATH_INODE | \
	 NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME | \
	 NEVERC_KRT_LAYOUT_CERT_TASK_THREADS | \
	 NEVERC_KRT_LAYOUT_CERT_TASK_WALK | \
	 NEVERC_KRT_LAYOUT_CERT_TASK_REF | \
	 NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE | \
	 NEVERC_KRT_LAYOUT_CERT_USER_PTMAP | \
	 NEVERC_KRT_LAYOUT_CERT_FILE_DENTRY)

/* ---- nvk_ksyms.c (shared with nvkmod.c) ---- */

extern _neverc_krt_sym_resolver_fn _neverc_krt_sym_resolver;
void _neverc_krt_cache_key_init(void);

/* ---- nvk_mem.c ---- */

extern int                  _neverc_krt_mem_inited;
extern neverc_krt_pte_rw_fn _neverc_krt_pte_make_rw;
extern neverc_krt_pte_rw_fn _neverc_krt_pte_make_ro;
int _neverc_krt_mem_nofault_available(void);

/* ---- nvk_usercopy.c ---- */

void _neverc_krt_usercopy_init(void);
unsigned long _neverc_krt_mem_copy_from_user_compat(
	void *to, const void __user *from, unsigned long n);
unsigned long _neverc_krt_mem_copy_to_user_compat(
	void __user *to, const void *from, unsigned long n);

/* ---- nvk_vma.c (shared with nvk_xmem.c) ---- */

extern neverc_krt_get_task_mm_fn    _neverc_krt_get_task_mm;
extern neverc_krt_mmput_fn          _neverc_krt_mmput;

/* ---- nvk_process.c ---- */

extern unsigned long _neverc_krt_off_comm;
int _neverc_krt_rcu_read_begin(void);
void _neverc_krt_rcu_read_end(void);
int _neverc_krt_rcu_available(void);
int _neverc_krt_task_walk_init(void);
int _neverc_krt_task_pid_available(void);
int _neverc_krt_task_ref_available(void);
int _neverc_krt_task_user_state_available(void);

/* ---- nvk_addr.c ---- */

int _neverc_krt_kernel_pointer_is_valid(const void *pointer);

/* ---- nvk_selinux.c ---- */

volatile int *_neverc_krt_se_probe_state(void *se_state);

/* ---- nvk_interpose.c ---- */

int _neverc_krt_patch_multi(u32 *target, u32 *insns, int count);

/* ---- nvk_compat.c ---- */

int _neverc_krt_version_setup(unsigned int profile_id);
const struct neverc_krt_runtime_caps *_neverc_krt_current_caps(void);
/* Active profile policy; -1 means bootstrap has not selected a profile. */
int _neverc_krt_current_kcfi_mode(void);
int _neverc_krt_current_profile_id(void);
const struct neverc_krt_gki_layout *_neverc_krt_get_gki_layout(void);
/* True on EXACT or same-series COMPAT.  `required` must be non-zero. */
int _neverc_krt_layout_fields_proven(unsigned long required);
unsigned long _neverc_krt_get_module_size(void);
unsigned long _neverc_krt_get_kimage_vaddr_base(void);
unsigned long _neverc_krt_get_file_dentry_off(void);

int neverc_krt_interpose_pause(struct neverc_krt_interpose *h);
int _neverc_krt_user_ptmap_claim_cleanup(void);
void _neverc_krt_user_ptmap_release_cleanup(void);

long _neverc_krt_sext(long value, int bits);

#endif /* NEVERC_KRT_INTERNAL_H */

#!/usr/bin/env python3
"""Generate Android GKI profile policy and runtime compatibility tables."""

import argparse
import json
from pathlib import Path
import re
import sys


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
DEFAULT_CATALOG = RUNTIME_ROOT / "arm64/gki-profiles.json"
DEFAULT_MANIFEST_ROOT = RUNTIME_ROOT / "arm64/gki-manifests"
DEFAULT_RELEASE_LOCK = RUNTIME_ROOT / "arm64/gki-release.json"
DEFAULT_LAYOUT_CERTIFICATES = (
    RUNTIME_ROOT / "arm64/gki-layout-certificates.json"
)
DEFAULT_PROFILE_IDS_HEADER = RUNTIME_ROOT / "include/nvk_profile_ids.h"
DEFAULT_PROFILE_HEADER = RUNTIME_ROOT / "include/nvk_profile_config.h"
DEFAULT_PROFILE_TABLE = RUNTIME_ROOT / "src/nvk_profile_table.inc"
DEFAULT_COMPAT_TABLE = RUNTIME_ROOT / "src/nvk_compat_table.inc"

KCFI_MODES = {
    "disabled": ("NEVERC_KRT_KCFI_MODE_DISABLED", 0),
    "classic": ("NEVERC_KRT_KCFI_MODE_CLASSIC", 1),
    "normalized": ("NEVERC_KRT_KCFI_MODE_NORMALIZED", 2),
}
SHADOW_CALL_STACK_MODES = {
    "static": ("NEVERC_KRT_SCS_MODE_STATIC", 1),
    "dynamic": ("NEVERC_KRT_SCS_MODE_DYNAMIC", 2),
}
CAPABILITY_ABIS = {
    "ftrace_callback_abi": {
        "pt_regs": (
            "NEVERC_KRT_PROFILE_FTRACE_CALLBACK_PT_REGS",
            "NEVERC_KRT_FTRACE_ABI_PT_REGS",
            1,
        ),
        "ftrace_regs": (
            "NEVERC_KRT_PROFILE_FTRACE_CALLBACK_FTRACE_REGS",
            "NEVERC_KRT_FTRACE_ABI_FTRACE_REGS",
            2,
        ),
    },
    "filldir_abi": {
        "returns_int": (
            "NEVERC_KRT_PROFILE_FILLDIR_RETURNS_INT",
            "NEVERC_KRT_FILLDIR_ABI_RETURNS_INT",
            1,
        ),
        "returns_bool": (
            "NEVERC_KRT_PROFILE_FILLDIR_RETURNS_BOOL",
            "NEVERC_KRT_FILLDIR_ABI_RETURNS_BOOL",
            2,
        ),
    },
    "kallsyms_iter_abi": {
        "with_module": (
            "NEVERC_KRT_PROFILE_KALLSYMS_WITH_MODULE",
            "NEVERC_KRT_KALLSYMS_ABI_WITH_MODULE",
            1,
        ),
        "address_only": (
            "NEVERC_KRT_PROFILE_KALLSYMS_ADDRESS_ONLY",
            "NEVERC_KRT_KALLSYMS_ABI_ADDRESS_ONLY",
            2,
        ),
    },
    "do_mmap_abi": {
        "without_vm_flags": (
            "NEVERC_KRT_PROFILE_DO_MMAP_WITHOUT_VM_FLAGS",
            "NEVERC_KRT_DO_MMAP_ABI_WITHOUT_VM_FLAGS",
            1,
        ),
        "with_vm_flags": (
            "NEVERC_KRT_PROFILE_DO_MMAP_WITH_VM_FLAGS",
            "NEVERC_KRT_DO_MMAP_ABI_WITH_VM_FLAGS",
            2,
        ),
    },
}
CAPABILITY_KEYS = frozenset({
    "do_mmap_abi",
    "filldir_abi",
    "ftrace_callback_abi",
    "ftrace_registration_api",
    "kallsyms_iter_abi",
})
PROFILE_KEYS = frozenset({
    "android_release",
    "capabilities",
    "kcfi_mode",
    "kernel_name",
    "kimage_vaddr",
    "kmi_generation",
    "legacy_id",
    "linux_major",
    "linux_minor",
    "linux_patch",
    "page_shift",
    "shadow_call_stack_mode",
    "symbol",
})
OPTIONAL_PROFILE_KEYS = frozenset({"aliases", "vermagic"})
EXPECTED_KCFI_TYPEIDS = {
    "disabled": None,
    "classic": {
        "cleanup_module": "0xa540670c",
        "init_module": "0x36b1c5a6",
    },
    "normalized": {
        "cleanup_module": "0xe5c47d60",
        "init_module": "0x6fbb3035",
    },
}
VERMAGIC_IDENTITY = re.compile(
    r"^(?P<major>[0-9]+)\.(?P<minor>[0-9]+)\.(?P<patch>[0-9]+)-"
    r"android(?P<android>[0-9]+)-(?P<kmi>[0-9]+)(?:[- ]|$)"
)
RELEASE_TOKEN_IDENTITY = re.compile(
    r"^(?P<major>[0-9]+)\.(?P<minor>[0-9]+)\.(?P<patch>[0-9]+)-"
    r"android(?P<android>[0-9]+)-(?P<kmi>[0-9]+)(?:-[^\s]+)*$"
)
SHA256_HEX = re.compile(r"^[0-9a-f]{64}$")
SYMBOL_NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1

LAYOUT_CERTIFICATE_BASE_KEYS = frozenset({
    "identity",
    "profile_id",
    "release_token",
})
LAYOUT_CERTIFICATE_EVIDENCE_KEYS = frozenset({"raw_btf", "raw_dwarf"})
LAYOUT_CERTIFICATE_FULL_KEYS = frozenset({
    "manifest_evidence",
    "runtime_layout",
})
LAYOUT_CERTIFICATE_FIELD_KEYS = frozenset({
    "dir_context",
    "file_dentry",
    "filldir_abi",
    "filename_name",
    "inode_times",
    "path_inode",
    "task_ref",
    "task_threads",
    "task_user_state",
    "task_walk",
    "user_ptmap",
})
LAYOUT_CERTIFICATE_PRIVATE_FIELD_KEYS = frozenset({
    "dir_context",
    "filename_name",
    "inode_times",
    "path_inode",
    "task_ref",
    "task_threads",
    "task_user_state",
    "task_walk",
    "user_ptmap",
})
LAYOUT_CERTIFICATE_IDENTITY_KEYS = frozenset({
    "android_release",
    "kmi_generation",
    "linux_major",
    "linux_minor",
    "linux_patch",
    "page_shift",
})
LAYOUT_CERTIFICATE_MANIFEST_EVIDENCE_KEYS = frozenset({
    "config_sha256",
    "layout_sha256",
    "vmlinux_build_id",
})


def dedicated_compile_family(profiles, identity):
    """Return the catalog ID that owns this Android/KMI, if one exists."""
    owner = None
    for profile in profiles:
        if (
            profile.get("linux_major") == identity.get("linux_major")
            and profile.get("linux_minor") == identity.get("linux_minor")
            and profile.get("android_release") == identity.get("android_release")
            and profile.get("kmi_generation") == identity.get("kmi_generation")
            and profile.get("page_shift") == identity.get("page_shift")
        ):
            legacy_id = profile.get("legacy_id")
            if owner is not None and owner != legacy_id:
                raise ValueError(
                    "duplicate compile family for "
                    f"{identity.get('linux_major')}.{identity.get('linux_minor')} "
                    f"android{identity.get('android_release')}-"
                    f"{identity.get('kmi_generation')}"
                )
            owner = legacy_id
    return owner
DIR_CONTEXT_LAYOUT_KEYS = frozenset({"member_sizes", "members", "size"})
DIR_CONTEXT_MEMBER_KEYS = frozenset({"actor", "pos"})
INODE_TIMES_LAYOUT_KEYS = frozenset({"member_sizes", "members", "size"})
INODE_TIMES_MEMBER_KEYS = frozenset({
    "atime_nsec",
    "atime_sec",
    "mtime_nsec",
    "mtime_sec",
})
PATH_INODE_LAYOUT_KEYS = frozenset({"dentry", "path"})
PATH_INODE_OBJECT_KEYS = frozenset({"member_sizes", "members", "size"})
PATH_DENTRY_MEMBER_KEYS = frozenset({"dentry"})
DENTRY_INODE_MEMBER_KEYS = frozenset({"d_inode"})
FILENAME_NAME_LAYOUT_KEYS = frozenset({"member_sizes", "members", "size"})
FILENAME_NAME_MEMBER_KEYS = frozenset({"name"})
TASK_THREADS_LAYOUT_KEYS = frozenset({"signal_struct", "task_struct"})
TASK_THREADS_OBJECT_KEYS = frozenset({"member_sizes", "members", "size"})
TASK_THREADS_TASK_MEMBER_KEYS = frozenset({
    "pid",
    "signal",
    "thread_node",
    "thread_pid",
})
TASK_THREADS_SIGNAL_MEMBER_KEYS = frozenset({"thread_head"})
TASK_PRIVATE_OBJECT_KEYS = frozenset({"member_sizes", "members", "size"})
TASK_WALK_LAYOUT_KEYS = frozenset({"cred", "task_struct"})
TASK_WALK_TASK_MEMBER_KEYS = frozenset({
    "comm",
    "group_leader",
    "mm",
    "parent",
    "real_cred",
    "real_parent",
    "tasks",
})
TASK_WALK_CRED_MEMBER_KEYS = frozenset({
    "egid",
    "euid",
    "fsgid",
    "fsuid",
    "gid",
    "sgid",
    "suid",
    "uid",
})
TASK_REF_LAYOUT_KEYS = frozenset({"task_struct"})
TASK_REF_TASK_MEMBER_KEYS = frozenset({"usage"})
TASK_USER_STATE_LAYOUT_KEYS = frozenset({"pt_regs", "task_struct"})
TASK_USER_STATE_TASK_MEMBER_KEYS = frozenset({
    "flags",
    "stack",
    "stack_refcount",
})
TASK_USER_STATE_PT_REGS_MEMBER_KEYS = frozenset({"pc", "pstate"})
USER_PTMAP_LAYOUT_KEYS = frozenset({
    "geometry", "mm_struct", "pt_regs", "vm_area_struct"
})
USER_PTMAP_GEOMETRY_KEYS = frozenset({
    "contiguous_bit",
    "contiguous_entries",
    "descriptor_address_mask",
    "index_bits",
    "page_shift",
    "pa_bits",
    "physical_address_mask",
    "physical_page_mask",
    "pgd_shift",
    "pgtable_levels",
    "pmd_shift",
    "pte_shift",
    "tlbi_all_asid",
    "va_bits",
})
USER_PTMAP_OBJECT_KEYS = frozenset({"member_sizes", "members", "size"})
USER_PTMAP_MM_MEMBER_KEYS = frozenset({
    "mm_count", "mmap_lock", "page_table_lock", "pgd"
})
USER_PTMAP_VMA_REQUIRED_KEYS = frozenset({"vm_end", "vm_start"})
USER_PTMAP_VMA_PUBLIC_KEYS = frozenset({"vm_flags", "vm_mm", "vm_pgoff"})
USER_PTMAP_VMA_MEMBER_KEYS = USER_PTMAP_VMA_REQUIRED_KEYS
USER_PTMAP_REGS_MEMBER_KEYS = frozenset({"pc", "pstate", "regs", "sp"})

PROFILE_NUMERIC_ALIASES = (
    ("NEVERC_KRT_KCFI_MODE", "NEVERC_KRT_PROFILE_KCFI_MODE"),
    ("NEVERC_KRT_OFF_LIST", "NEVERC_KRT_PROFILE_MODULE_LIST_OFFSET"),
    ("NEVERC_KRT_OFF_NAME", "NEVERC_KRT_PROFILE_MODULE_NAME_OFFSET"),
    ("NEVERC_KRT_OFF_INIT", "NEVERC_KRT_PROFILE_MODULE_INIT_OFFSET"),
    ("NEVERC_KRT_OFF_EXIT", "NEVERC_KRT_PROFILE_MODULE_EXIT_OFFSET"),
    ("NEVERC_KRT_MODULE_SIZE", "NEVERC_KRT_PROFILE_MODULE_SIZE"),
    (
        "NEVERC_KRT_FILE_DENTRY_OFF",
        "NEVERC_KRT_PROFILE_FILE_DENTRY_OFFSET",
    ),
    (
        "NEVERC_KRT_FOPS_SIZE",
        "NEVERC_KRT_PROFILE_FILE_OPERATIONS_SIZE",
    ),
    (
        "NEVERC_KRT_DENTRY_DNAME_OFF",
        "NEVERC_KRT_PROFILE_DENTRY_NAME_OFFSET",
    ),
    ("NEVERC_KRT_SKC_DPORT_OFF", "NEVERC_KRT_PROFILE_SOCK_DPORT_OFFSET"),
    ("NEVERC_KRT_SKC_NUM_OFF", "NEVERC_KRT_PROFILE_SOCK_NUM_OFFSET"),
    (
        "NEVERC_KRT_TASK_PREEMPT_COUNT",
        "NEVERC_KRT_PROFILE_TASK_PREEMPT_COUNT_OFFSET",
    ),
    ("NEVERC_KRT_TASK_CPU", "NEVERC_KRT_PROFILE_TASK_CPU_OFFSET"),
    (
        "NEVERC_KRT_HAS_TASK_CPU_OFFSET",
        "NEVERC_KRT_PROFILE_HAS_TASK_CPU_OFFSET",
    ),
    ("NEVERC_KRT_NR_CPUS", "NEVERC_KRT_PROFILE_NR_CPUS"),
    ("NEVERC_KRT_PAGE_SHIFT", "NEVERC_KRT_PROFILE_PAGE_SHIFT"),
    ("NEVERC_KRT_VA_BITS", "NEVERC_KRT_PROFILE_VA_BITS"),
    ("NEVERC_KRT_PA_BITS", "NEVERC_KRT_PROFILE_PA_BITS"),
    (
        "NEVERC_KRT_PGTABLE_LEVELS",
        "NEVERC_KRT_PROFILE_PGTABLE_LEVELS",
    ),
)
PROFILE_STRING_ALIASES = (
    ("NEVERC_KRT_VERMAGIC", "NEVERC_KRT_PROFILE_VERMAGIC"),
    ("NEVERC_KRT_KERNEL_STR", "NEVERC_KRT_PROFILE_KERNEL_NAME"),
)

LAYOUT_FIELDS = (
    ("task_size", "task_struct", None),
    ("task_tasks", "task_struct", "tasks"),
    ("task_usage", "task_struct", "usage"),
    ("task_stack", "task_struct", "stack"),
    ("task_stack_refcount", "task_struct", "stack_refcount"),
    ("task_flags", "task_struct", "flags"),
    ("task_mm", "task_struct", "mm"),
    ("task_pid", "task_struct", "pid"),
    ("task_tgid", "task_struct", "tgid"),
    ("task_parent", "task_struct", "parent"),
    ("task_real_parent", "task_struct", "real_parent"),
    ("task_thread_pid", "task_struct", "thread_pid"),
    ("task_signal", "task_struct", "signal"),
    ("task_thread_node", "task_struct", "thread_node"),
    ("task_group_leader", "task_struct", "group_leader"),
    ("task_real_cred", "task_struct", "real_cred"),
    ("task_cred", "task_struct", "cred"),
    ("task_comm", "task_struct", "comm"),
    ("task_comm_size", "task_struct", ("member_size", "comm")),
    ("task_nsproxy", "task_struct", "nsproxy"),
    ("task_seccomp", "task_struct", "seccomp"),
    ("signal_size", "signal_struct", None),
    ("signal_thread_head", "signal_struct", "thread_head"),
    ("pt_regs_size", "pt_regs", None),
    ("pt_regs_regs", "pt_regs", "regs"),
    ("pt_regs_regs_size", "pt_regs", ("member_size", "regs")),
    ("pt_regs_sp", "pt_regs", "sp"),
    ("pt_regs_sp_size", "pt_regs", ("member_size", "sp")),
    ("pt_regs_pc", "pt_regs", "pc"),
    ("pt_regs_pc_size", "pt_regs", ("member_size", "pc")),
    ("pt_regs_pstate", "pt_regs", "pstate"),
    ("pt_regs_pstate_size", "pt_regs", ("member_size", "pstate")),
    ("cred_size", "cred", None),
    ("cred_uid", "cred", "uid"),
    ("cred_gid", "cred", "gid"),
    ("cred_suid", "cred", "suid"),
    ("cred_sgid", "cred", "sgid"),
    ("cred_euid", "cred", "euid"),
    ("cred_egid", "cred", "egid"),
    ("cred_fsuid", "cred", "fsuid"),
    ("cred_fsgid", "cred", "fsgid"),
    ("cred_securebits", "cred", "securebits"),
    ("cred_cap_inheritable", "cred", "cap_inheritable"),
    ("cred_cap_permitted", "cred", "cap_permitted"),
    ("cred_cap_effective", "cred", "cap_effective"),
    ("cred_cap_bset", "cred", "cap_bset"),
    ("cred_cap_ambient", "cred", "cap_ambient"),
    ("mm_size", "mm_struct", None),
    ("mm_count", "mm_struct", "mm_count"),
    ("mm_count_size", "mm_struct", ("member_size", "mm_count")),
    ("mm_pgd", "mm_struct", "pgd"),
    ("mm_pgd_size", "mm_struct", ("member_size", "pgd")),
    ("mm_page_table_lock", "mm_struct", "page_table_lock"),
    ("mm_page_table_lock_size", "mm_struct", ("member_size", "page_table_lock")),
    ("mm_mmap_lock", "mm_struct", "mmap_lock"),
    ("mm_mmap_lock_size", "mm_struct", ("member_size", "mmap_lock")),
    ("vma_size", "vm_area_struct", None),
    ("vma_start", "vm_area_struct", "vm_start"),
    ("vma_start_size", "vm_area_struct", ("member_size", "vm_start")),
    ("vma_end", "vm_area_struct", "vm_end"),
    ("vma_end_size", "vm_area_struct", ("member_size", "vm_end")),
    ("vma_mm", "vm_area_struct", "vm_mm"),
    ("vma_page_prot", "vm_area_struct", "vm_page_prot"),
    ("vma_flags", "vm_area_struct", "vm_flags"),
    ("vma_pgoff", "vm_area_struct", "vm_pgoff"),
    ("vma_file", "vm_area_struct", "vm_file"),
    ("dir_context_size", "dir_context", None),
    ("dir_context_actor", "dir_context", "actor"),
    ("dir_context_actor_size", "dir_context", ("member_size", "actor")),
    ("dir_context_pos", "dir_context", "pos"),
    ("dir_context_pos_size", "dir_context", ("member_size", "pos")),
    ("vmap_va_start", "vmap_area", "va_start"),
    ("vmap_va_end", "vmap_area", "va_end"),
    ("module_list", "module", "list"),
    ("module_name", "module", "name"),
    ("module_kobj", (("module", "mkobj"), ("module_kobject", "kobj")), None),
    ("kobject_name", "kobject", "name"),
    ("skb_data", "sk_buff", "data"),
    ("sock_dport", "sock_common", "skc_dport"),
    ("sock_num", "sock_common", "skc_num"),
    ("nsproxy_mnt_ns", "nsproxy", "mnt_ns"),
    ("nsproxy_net_ns", "nsproxy", "net_ns"),
    ("seccomp_mode", "seccomp", "mode"),
    ("kstat_size", "kstat", None),
    ("kstat_mode", "kstat", "mode"),
    ("kstat_uid", "kstat", "uid"),
    ("kstat_gid", "kstat", "gid"),
    ("kstat_file_size", "kstat", "size"),
    ("dentry_name", (("dentry", "d_name"), ("qstr", "name")), None),
    ("file_dentry", (("file", "f_path"), ("path", "dentry")), None),
    ("module_size", "module", None),
    ("module_init", "module", "init"),
    ("module_exit", "module", "exit"),
    ("filename_size", "filename", None),
    ("filename_name", "filename", "name"),
    ("filename_name_size", "filename", ("member_size", "name")),
    ("path_size", "path", None),
    ("path_dentry", "path", "dentry"),
    ("path_dentry_size", "path", ("member_size", "dentry")),
    ("dentry_size", "dentry", None),
    ("dentry_inode", "dentry", "d_inode"),
    ("dentry_inode_size", "dentry", ("member_size", "d_inode")),
)

RUNTIME_LAYOUT_USER_GEOMETRY_FIELDS = (
    ("user_page_shift", "page_shift"),
    ("user_va_bits", "va_bits"),
    ("user_pa_bits", "pa_bits"),
    ("user_pgtable_levels", "pgtable_levels"),
    ("user_pgd_shift", "pgd_shift"),
    ("user_pmd_shift", "pmd_shift"),
    ("user_pte_shift", "pte_shift"),
    ("user_index_bits", "index_bits"),
    ("user_contiguous_bit", "contiguous_bit"),
    ("user_contiguous_entries", "contiguous_entries"),
    ("user_descriptor_address_mask", "descriptor_address_mask"),
    ("user_physical_address_mask", "physical_address_mask"),
    ("user_physical_page_mask", "physical_page_mask"),
    ("user_tlbi_all_asid", "tlbi_all_asid"),
)
RUNTIME_LAYOUT_INODE_FIELDS = (
    ("inode_size", None, None),
    ("inode_atime_sec", "members", "atime_sec"),
    ("inode_atime_sec_size", "member_sizes", "atime_sec"),
    ("inode_mtime_sec", "members", "mtime_sec"),
    ("inode_mtime_sec_size", "member_sizes", "mtime_sec"),
    ("inode_atime_nsec", "members", "atime_nsec"),
    ("inode_atime_nsec_size", "member_sizes", "atime_nsec"),
    ("inode_mtime_nsec", "members", "mtime_nsec"),
    ("inode_mtime_nsec_size", "member_sizes", "mtime_nsec"),
)
RUNTIME_LAYOUT_FIXED_FIELDS = (
    ("ftrace_ops_func", 0),
    ("ftrace_ops_flags", 16),
)
RUNTIME_LAYOUT_FIELD_NAMES = (
    tuple(output_name for output_name, _structure, _field in LAYOUT_FIELDS)
    + ("task_stack_size",)
    + tuple(output_name for output_name, _value in RUNTIME_LAYOUT_FIXED_FIELDS)
    + tuple(
        output_name
        for output_name, _container, _member_name in RUNTIME_LAYOUT_INODE_FIELDS
    )
    + tuple(
        output_name
        for output_name, _geometry_name in RUNTIME_LAYOUT_USER_GEOMETRY_FIELDS
    )
)

# Loader / inode facts compile_layout_contract reads that are not LAYOUT_FIELDS rows.
LOADER_LAYOUT_MEMBERS = (
    ("module", "init"),
    ("module", "exit"),
    ("file", "f_path"),
    ("thread_info", "preempt_count"),
    ("thread_info", "cpu"),
)
SIZEONLY_LAYOUT_STRUCTS = ("file_operations",)
INODE_TIME_SOURCE_FIELDS = frozenset({
    "i_atime",
    "i_mtime",
    "i_atime_sec",
    "i_mtime_sec",
    "i_atime_nsec",
    "i_mtime_nsec",
})
TIMESPEC64_MEMBER_KEYS = frozenset({"tv_sec", "tv_nsec"})


def _add_read_member(members, structure, field):
    if not isinstance(structure, str) or not isinstance(field, str) or not field:
        return
    members.setdefault(structure, set()).add(field)


def neverc_read_members_by_struct():
    """Struct -> members the profile contract and layout certificates read."""
    members = {}
    for _name, structure, field in LAYOUT_FIELDS:
        if isinstance(structure, tuple):
            for nested_structure, nested_field in structure:
                _add_read_member(members, nested_structure, nested_field)
            continue
        if isinstance(field, tuple) and field and field[0] == "member_size":
            _add_read_member(members, structure, field[1])
            continue
        if field is None:
            members.setdefault(structure, set())
            continue
        _add_read_member(members, structure, field)
    for structure, field in LOADER_LAYOUT_MEMBERS:
        _add_read_member(members, structure, field)
    for field in INODE_TIME_SOURCE_FIELDS:
        _add_read_member(members, "inode", field)
    for field in TIMESPEC64_MEMBER_KEYS:
        _add_read_member(members, "timespec64", field)
    for structure in SIZEONLY_LAYOUT_STRUCTS:
        members.setdefault(structure, set())
    return {
        structure: frozenset(fields) for structure, fields in sorted(members.items())
    }


def member(layouts, structure, field):
    try:
        return layouts[structure]["members"][field]
    except KeyError as error:
        raise ValueError(f"manifest lacks {structure}.{field}") from error


def member_size(layouts, structure, field):
    try:
        return layouts[structure]["member_sizes"][field]
    except KeyError as error:
        raise ValueError(
            f"manifest lacks sizeof({structure}.{field})"
        ) from error


def layout_value(layouts, structure, field):
    if isinstance(structure, tuple):
        return sum(
            member(layouts, nested_structure, nested_field)
            for nested_structure, nested_field in structure
        )
    if field is None:
        try:
            return layouts[structure]["size"]
        except KeyError as error:
            raise ValueError(f"manifest lacks sizeof({structure})") from error
    if isinstance(field, tuple) and field[0] == "member_size":
        return member_size(layouts, structure, field[1])
    return member(layouts, structure, field)


def validate_dir_context_layout(layouts, context):
    size = layout_value(layouts, "dir_context", None)
    actor = member(layouts, "dir_context", "actor")
    actor_size = member_size(layouts, "dir_context", "actor")
    pos = member(layouts, "dir_context", "pos")
    pos_size = member_size(layouts, "dir_context", "pos")
    values = (size, actor, actor_size, pos, pos_size)

    if any(
        not isinstance(value, int) or isinstance(value, bool) or value < 0
        for value in values
    ):
        raise ValueError(f"{context}: invalid dir_context layout value")
    if size == 0 or size > 64:
        raise ValueError(f"{context}: unsupported dir_context size")
    if actor_size != 8 or pos_size != 8:
        raise ValueError(f"{context}: unsupported dir_context field width")
    if actor > size or actor_size > size - actor:
        raise ValueError(f"{context}: dir_context.actor is out of bounds")
    if pos > size or pos_size > size - pos:
        raise ValueError(f"{context}: dir_context.pos is out of bounds")
    if not (actor + actor_size <= pos or pos + pos_size <= actor):
        raise ValueError(f"{context}: dir_context fields overlap")


def normalize_filename_name_layout(layouts, context):
    try:
        normalized = {
            "size": layouts["filename"]["size"],
            "members": {"name": layouts["filename"]["members"]["name"]},
            "member_sizes": {
                "name": layouts["filename"]["member_sizes"]["name"]
            },
        }
    except (KeyError, TypeError) as error:
        raise ValueError(
            f"{context}: filename_name lacks member width evidence"
        ) from error
    validate_filename_name_layout(normalized, context)
    return normalized


def validate_filename_name_layout(layout, context):
    if (
        not isinstance(layout, dict)
        or set(layout) != FILENAME_NAME_LAYOUT_KEYS
        or not isinstance(layout.get("members"), dict)
        or set(layout["members"]) != FILENAME_NAME_MEMBER_KEYS
        or not isinstance(layout.get("member_sizes"), dict)
        or set(layout["member_sizes"]) != FILENAME_NAME_MEMBER_KEYS
    ):
        raise ValueError(f"{context}: filename_name keys do not match schema")

    size = layout.get("size")
    offset = layout["members"].get("name")
    width = layout["member_sizes"].get("name")
    values = (size, offset, width)
    if any(
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > UINT64_MAX
        for value in values
    ) or size == 0:
        raise ValueError(f"{context}: invalid filename_name layout value")
    if width != 8:
        raise ValueError(
            f"{context}: unsupported filename_name.name field width"
        )
    if offset > size or width > size - offset:
        raise ValueError(f"{context}: filename_name.name is out of bounds")


def validate_inode_times_layout(layout, context):
    if (
        not isinstance(layout, dict)
        or set(layout) != INODE_TIMES_LAYOUT_KEYS
        or not isinstance(layout.get("members"), dict)
        or set(layout["members"]) != INODE_TIMES_MEMBER_KEYS
        or not isinstance(layout.get("member_sizes"), dict)
        or set(layout["member_sizes"]) != INODE_TIMES_MEMBER_KEYS
    ):
        raise ValueError(f"{context}: inode_times keys do not match schema")

    size = layout.get("size")
    if (
        not isinstance(size, int)
        or isinstance(size, bool)
        or size <= 0
        or size > UINT64_MAX
    ):
        raise ValueError(f"{context}: inode_times size must be positive uint64")

    fields = []
    for name in sorted(INODE_TIMES_MEMBER_KEYS):
        offset = layout["members"].get(name)
        width = layout["member_sizes"].get(name)
        if (
            not isinstance(offset, int)
            or isinstance(offset, bool)
            or offset < 0
            or not isinstance(width, int)
            or isinstance(width, bool)
            or width <= 0
        ):
            raise ValueError(f"{context}: invalid inode_times.{name} layout value")
        expected_widths = (8,) if name.endswith("_sec") else (4, 8)
        if width not in expected_widths:
            raise ValueError(
                f"{context}: unsupported inode_times.{name} field width"
            )
        if offset > size or width > size - offset:
            raise ValueError(f"{context}: inode_times.{name} is out of bounds")
        fields.append((name, offset, width))

    for index, (left_name, left_offset, left_width) in enumerate(fields):
        for right_name, right_offset, right_width in fields[index + 1:]:
            if not (
                left_offset + left_width <= right_offset
                or right_offset + right_width <= left_offset
            ):
                raise ValueError(
                    f"{context}: inode_times fields overlap: "
                    f"{left_name}, {right_name}"
                )


def normalize_inode_times_layout(layouts, context):
    """Normalize split or timespec64 inode timestamps to four scalars."""
    inode = layouts.get("inode")
    if inode is None:
        return None
    if not isinstance(inode, dict):
        raise ValueError(f"{context}: inode layout must be an object")
    members = inode.get("members")
    member_sizes = inode.get("member_sizes")
    size = inode.get("size")
    if not isinstance(members, dict) or not isinstance(member_sizes, dict):
        raise ValueError(f"{context}: inode layout lacks member width evidence")

    split_names = {
        "atime_sec": "i_atime_sec",
        "mtime_sec": "i_mtime_sec",
        "atime_nsec": "i_atime_nsec",
        "mtime_nsec": "i_mtime_nsec",
    }
    if all(name in members and name in member_sizes for name in split_names.values()):
        normalized = {
            "size": size,
            "members": {
                output: members[source]
                for output, source in split_names.items()
            },
            "member_sizes": {
                output: member_sizes[source]
                for output, source in split_names.items()
            },
        }
        validate_inode_times_layout(normalized, context)
        return normalized

    compound_names = ("i_atime", "i_mtime")
    if not all(name in members and name in member_sizes for name in compound_names):
        raise ValueError(
            f"{context}: inode lacks complete split or timespec64 timestamp evidence"
        )
    timespec = layouts.get("timespec64")
    if not isinstance(timespec, dict):
        raise ValueError(f"{context}: inode timespec layout lacks timespec64")
    timespec_members = timespec.get("members")
    timespec_member_sizes = timespec.get("member_sizes")
    timespec_size = timespec.get("size")
    if (
        not isinstance(timespec_members, dict)
        or not isinstance(timespec_member_sizes, dict)
        or "tv_sec" not in timespec_members
        or "tv_nsec" not in timespec_members
        or "tv_sec" not in timespec_member_sizes
        or "tv_nsec" not in timespec_member_sizes
        or not isinstance(timespec_size, int)
        or isinstance(timespec_size, bool)
        or timespec_size <= 0
    ):
        raise ValueError(f"{context}: incomplete timespec64 scalar evidence")
    timespec_fields = []
    for name, expected_widths in (("tv_sec", (8,)), ("tv_nsec", (4, 8))):
        offset = timespec_members[name]
        width = timespec_member_sizes[name]
        if (
            not isinstance(offset, int)
            or isinstance(offset, bool)
            or offset < 0
            or not isinstance(width, int)
            or isinstance(width, bool)
            or width not in expected_widths
            or offset > timespec_size
            or width > timespec_size - offset
        ):
            raise ValueError(f"{context}: invalid timespec64.{name} scalar evidence")
        timespec_fields.append((offset, width))
    if not (
        timespec_fields[0][0] + timespec_fields[0][1]
        <= timespec_fields[1][0]
        or timespec_fields[1][0] + timespec_fields[1][1]
        <= timespec_fields[0][0]
    ):
        raise ValueError(f"{context}: timespec64 scalar fields overlap")
    for name in compound_names:
        offset = members[name]
        width = member_sizes[name]
        if width != timespec_size:
            raise ValueError(f"{context}: inode.{name} width mismatches timespec64")
        if (
            not isinstance(offset, int)
            or isinstance(offset, bool)
            or offset < 0
            or not isinstance(size, int)
            or isinstance(size, bool)
            or offset > size
            or width > size - offset
        ):
            raise ValueError(f"{context}: inode.{name} is out of bounds")

    normalized = {
        "size": size,
        "members": {
            "atime_sec": members["i_atime"] + timespec_members["tv_sec"],
            "atime_nsec": members["i_atime"] + timespec_members["tv_nsec"],
            "mtime_sec": members["i_mtime"] + timespec_members["tv_sec"],
            "mtime_nsec": members["i_mtime"] + timespec_members["tv_nsec"],
        },
        "member_sizes": {
            "atime_sec": timespec_member_sizes["tv_sec"],
            "atime_nsec": timespec_member_sizes["tv_nsec"],
            "mtime_sec": timespec_member_sizes["tv_sec"],
            "mtime_nsec": timespec_member_sizes["tv_nsec"],
        },
    }
    validate_inode_times_layout(normalized, context)
    return normalized


def normalize_path_inode_layout(layouts, context):
    try:
        normalized = {
            "path": {
                "size": layouts["path"]["size"],
                "members": {"dentry": layouts["path"]["members"]["dentry"]},
                "member_sizes": {
                    "dentry": layouts["path"]["member_sizes"]["dentry"]
                },
            },
            "dentry": {
                "size": layouts["dentry"]["size"],
                "members": {
                    "d_inode": layouts["dentry"]["members"]["d_inode"]
                },
                "member_sizes": {
                    "d_inode": layouts["dentry"]["member_sizes"]["d_inode"]
                },
            },
        }
    except (KeyError, TypeError) as error:
        raise ValueError(f"{context}: path_inode lacks member width evidence") from error
    validate_path_inode_layout(normalized, context)
    return normalized


def validate_path_inode_layout(layout, context):
    if not isinstance(layout, dict) or set(layout) != PATH_INODE_LAYOUT_KEYS:
        raise ValueError(f"{context}: path_inode keys do not match schema")
    specifications = (
        ("path", "dentry", PATH_DENTRY_MEMBER_KEYS),
        ("dentry", "d_inode", DENTRY_INODE_MEMBER_KEYS),
    )
    for object_name, member_name, member_keys in specifications:
        object_layout = layout.get(object_name)
        if (
            not isinstance(object_layout, dict)
            or set(object_layout) != PATH_INODE_OBJECT_KEYS
            or not isinstance(object_layout.get("members"), dict)
            or set(object_layout["members"]) != member_keys
            or not isinstance(object_layout.get("member_sizes"), dict)
            or set(object_layout["member_sizes"]) != member_keys
        ):
            raise ValueError(
                f"{context}: path_inode.{object_name} keys do not match schema"
            )
        size = object_layout.get("size")
        offset = object_layout["members"].get(member_name)
        width = object_layout["member_sizes"].get(member_name)
        values = (size, offset, width)
        if any(
            not isinstance(value, int)
            or isinstance(value, bool)
            or value < 0
            or value > UINT64_MAX
            for value in values
        ) or size == 0:
            raise ValueError(
                f"{context}: invalid path_inode.{object_name} layout value"
            )
        if object_name == "path" and size != 16:
            raise ValueError(f"{context}: unsupported path_inode.path size")
        if width != 8:
            raise ValueError(
                f"{context}: unsupported path_inode.{object_name}.{member_name} "
                "field width"
            )
        if offset > size or width > size - offset:
            raise ValueError(
                f"{context}: path_inode.{object_name}.{member_name} is out of bounds"
            )


def validate_task_threads_layout(layout, context):
    if not isinstance(layout, dict) or set(layout) != TASK_THREADS_LAYOUT_KEYS:
        raise ValueError(f"{context}: task_threads keys do not match schema")

    specifications = (
        (
            "task_struct",
            TASK_THREADS_TASK_MEMBER_KEYS,
            {"pid": 4, "signal": 8, "thread_node": 16, "thread_pid": 8},
        ),
        (
            "signal_struct",
            TASK_THREADS_SIGNAL_MEMBER_KEYS,
            {"thread_head": 16},
        ),
    )
    for object_name, member_keys, expected_widths in specifications:
        object_layout = layout.get(object_name)
        if (
            not isinstance(object_layout, dict)
            or set(object_layout) != TASK_THREADS_OBJECT_KEYS
            or not isinstance(object_layout.get("members"), dict)
            or set(object_layout["members"]) != member_keys
            or not isinstance(object_layout.get("member_sizes"), dict)
            or set(object_layout["member_sizes"]) != member_keys
        ):
            raise ValueError(
                f"{context}: task_threads.{object_name} keys do not match schema"
            )

        size = object_layout.get("size")
        if (
            not isinstance(size, int)
            or isinstance(size, bool)
            or size <= 0
            or size > UINT64_MAX
        ):
            raise ValueError(
                f"{context}: invalid task_threads.{object_name} size"
            )
        for member_name in sorted(member_keys):
            offset = object_layout["members"].get(member_name)
            width = object_layout["member_sizes"].get(member_name)
            if (
                not isinstance(offset, int)
                or isinstance(offset, bool)
                or offset < 0
                or offset > UINT64_MAX
                or width != expected_widths[member_name]
            ):
                raise ValueError(
                    f"{context}: invalid task_threads.{object_name}."
                    f"{member_name} layout value"
                )
            if offset > size or width > size - offset:
                raise ValueError(
                    f"{context}: task_threads.{object_name}.{member_name} "
                    "is out of bounds"
                )


def validate_task_private_layout(
    layout, context, field_name, layout_keys, specifications
):
    if not isinstance(layout, dict) or set(layout) != layout_keys:
        raise ValueError(f"{context}: {field_name} keys do not match schema")

    for object_name, member_keys, expected_widths in specifications:
        object_layout = layout.get(object_name)
        if (
            not isinstance(object_layout, dict)
            or set(object_layout) != TASK_PRIVATE_OBJECT_KEYS
            or not isinstance(object_layout.get("members"), dict)
            or set(object_layout["members"]) != member_keys
            or not isinstance(object_layout.get("member_sizes"), dict)
            or set(object_layout["member_sizes"]) != member_keys
        ):
            raise ValueError(
                f"{context}: {field_name}.{object_name} keys do not match schema"
            )

        size = object_layout.get("size")
        if (
            not isinstance(size, int)
            or isinstance(size, bool)
            or size <= 0
            or size > UINT64_MAX
        ):
            raise ValueError(
                f"{context}: invalid {field_name}.{object_name} size"
            )
        for member_name in sorted(member_keys):
            offset = object_layout["members"].get(member_name)
            width = object_layout["member_sizes"].get(member_name)
            if (
                not isinstance(offset, int)
                or isinstance(offset, bool)
                or offset < 0
                or offset > UINT64_MAX
                or width != expected_widths[member_name]
            ):
                raise ValueError(
                    f"{context}: invalid {field_name}.{object_name}."
                    f"{member_name} layout value"
                )
            if offset > size or width > size - offset:
                raise ValueError(
                    f"{context}: {field_name}.{object_name}.{member_name} "
                    "is out of bounds"
                )


def validate_task_walk_layout(layout, context):
    validate_task_private_layout(
        layout,
        context,
        "task_walk",
        TASK_WALK_LAYOUT_KEYS,
        (
            (
                "task_struct",
                TASK_WALK_TASK_MEMBER_KEYS,
                {
                    "comm": 16,
                    "group_leader": 8,
                    "mm": 8,
                    "parent": 8,
                    "real_cred": 8,
                    "real_parent": 8,
                    "tasks": 16,
                },
            ),
            (
                "cred",
                TASK_WALK_CRED_MEMBER_KEYS,
                {name: 4 for name in TASK_WALK_CRED_MEMBER_KEYS},
            ),
        ),
    )


def validate_task_ref_layout(layout, context):
    validate_task_private_layout(
        layout,
        context,
        "task_ref",
        TASK_REF_LAYOUT_KEYS,
        (("task_struct", TASK_REF_TASK_MEMBER_KEYS, {"usage": 4}),),
    )


def validate_task_user_state_layout(layout, context):
    validate_task_private_layout(
        layout,
        context,
        "task_user_state",
        TASK_USER_STATE_LAYOUT_KEYS,
        (
            (
                "task_struct",
                TASK_USER_STATE_TASK_MEMBER_KEYS,
                {"flags": 4, "stack": 8, "stack_refcount": 4},
            ),
            (
                "pt_regs",
                TASK_USER_STATE_PT_REGS_MEMBER_KEYS,
                {"pc": 8, "pstate": 8},
            ),
        ),
    )


def validate_user_ptmap_layout(layout, context, expected_geometry=None):
    if not isinstance(layout, dict) or set(layout) != USER_PTMAP_LAYOUT_KEYS:
        raise ValueError(f"{context}: user_ptmap keys do not match schema")

    geometry = layout.get("geometry")
    if (
        not isinstance(geometry, dict)
        or set(geometry) != USER_PTMAP_GEOMETRY_KEYS
    ):
        raise ValueError(
            f"{context}: user_ptmap.geometry keys do not match schema"
        )
    for name in sorted(USER_PTMAP_GEOMETRY_KEYS):
        value = geometry.get(name)
        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or value <= 0
            or value > UINT64_MAX
        ):
            raise ValueError(
                f"{context}: invalid user_ptmap.geometry.{name} value"
            )
    if (
        geometry["page_shift"] != 12
        or geometry["va_bits"] != 39
        or geometry["pa_bits"] != 48
        or geometry["pgtable_levels"] != 3
        or geometry["pgd_shift"] != 30
        or geometry["pmd_shift"] != 21
        or geometry["pte_shift"] != 12
        or geometry["index_bits"] != 9
        or geometry["contiguous_bit"] != 52
        or geometry["contiguous_entries"] != 16
        or geometry["descriptor_address_mask"] not in (
            0x0000FFFFFFFFF000,
            0x0003FFFFFFFFF000,
        )
        or geometry["physical_address_mask"] != 0x0000FFFFFFFFFFFF
        or geometry["physical_page_mask"] != 0x0000FFFFFFFFF000
        or geometry["tlbi_all_asid"] != 1
    ):
        raise ValueError(f"{context}: unsupported user_ptmap geometry")
    if expected_geometry is not None and geometry != expected_geometry:
        raise ValueError(
            f"{context}: user_ptmap geometry mismatches profile family"
        )

    specifications = (
        (
            "mm_struct",
            USER_PTMAP_MM_MEMBER_KEYS,
            {
                "mm_count": 4,
                "mmap_lock": 64,
                "page_table_lock": 4,
                "pgd": 8,
            },
        ),
        (
            "vm_area_struct",
            USER_PTMAP_VMA_REQUIRED_KEYS,
            {
                "vm_end": 8,
                "vm_flags": 8,
                "vm_mm": 8,
                "vm_pgoff": 8,
                "vm_start": 8,
            },
        ),
        (
            "pt_regs",
            USER_PTMAP_REGS_MEMBER_KEYS,
            {"pc": 8, "pstate": 8, "regs": 248, "sp": 8},
        ),
    )
    for object_name, member_keys, expected_widths in specifications:
        object_layout = layout.get(object_name)
        allowed_members = member_keys
        if object_name == "vm_area_struct":
            allowed_members = member_keys | USER_PTMAP_VMA_PUBLIC_KEYS
        actual_members = (
            set(object_layout.get("members", {}))
            if isinstance(object_layout, dict)
            and isinstance(object_layout.get("members"), dict)
            else set()
        )
        actual_sizes = (
            set(object_layout.get("member_sizes", {}))
            if isinstance(object_layout, dict)
            and isinstance(object_layout.get("member_sizes"), dict)
            else set()
        )
        if (
            not isinstance(object_layout, dict)
            or set(object_layout) != USER_PTMAP_OBJECT_KEYS
            or actual_members != actual_sizes
            or not member_keys.issubset(actual_members)
            or not actual_members.issubset(allowed_members)
        ):
            raise ValueError(
                f"{context}: user_ptmap.{object_name} keys do not match schema"
            )
        size = object_layout.get("size")
        if (
            not isinstance(size, int)
            or isinstance(size, bool)
            or size <= 0
            or size > UINT64_MAX
        ):
            raise ValueError(
                f"{context}: invalid user_ptmap.{object_name} size"
            )
        spans = []
        for member_name in sorted(actual_members):
            offset = object_layout["members"].get(member_name)
            width = object_layout["member_sizes"].get(member_name)
            if (
                not isinstance(offset, int)
                or isinstance(offset, bool)
                or offset < 0
                or offset > UINT64_MAX
                or width != expected_widths[member_name]
            ):
                raise ValueError(
                    f"{context}: invalid user_ptmap.{object_name}."
                    f"{member_name} layout value"
                )
            if offset > size or width > size - offset:
                raise ValueError(
                    f"{context}: user_ptmap.{object_name}.{member_name} "
                    "is out of bounds"
                )
            spans.append((offset, offset + width, member_name))
        spans.sort()
        for previous, current in zip(spans, spans[1:]):
            if previous[1] > current[0]:
                raise ValueError(
                    f"{context}: user_ptmap.{object_name} fields overlap"
                )


def normalize_user_ptmap_layout(manifest, context):
    layouts = manifest.get("layouts", {})
    config = manifest.get("config", {})
    profile_id = manifest.get("profile")
    descriptor_address_mask = (
        0x0003FFFFFFFFF000
        if profile_id in (612, 618)
        else 0x0000FFFFFFFFF000
    )
    try:
        normalized = {
            "geometry": {
                "page_shift": config["PAGE_SHIFT"],
                "va_bits": config["CONFIG_ARM64_VA_BITS"],
                "pa_bits": config["CONFIG_ARM64_PA_BITS"],
                "pgtable_levels": config["CONFIG_PGTABLE_LEVELS"],
                "pgd_shift": 30,
                "pmd_shift": 21,
                "pte_shift": 12,
                "index_bits": 9,
                "contiguous_bit": 52,
                "contiguous_entries": 16,
                "descriptor_address_mask": descriptor_address_mask,
                "physical_address_mask": 0x0000FFFFFFFFFFFF,
                "physical_page_mask": 0x0000FFFFFFFFF000,
                "tlbi_all_asid": 1,
            },
            "mm_struct": {
                "size": layouts["mm_struct"]["size"],
                "members": {
                    name: layouts["mm_struct"]["members"][name]
                    for name in USER_PTMAP_MM_MEMBER_KEYS
                },
                "member_sizes": {
                    name: layouts["mm_struct"]["member_sizes"][name]
                    for name in USER_PTMAP_MM_MEMBER_KEYS
                },
            },
            "vm_area_struct": {
                "size": layouts["vm_area_struct"]["size"],
                "members": {
                    name: layouts["vm_area_struct"]["members"][name]
                    for name in USER_PTMAP_VMA_MEMBER_KEYS
                },
                "member_sizes": {
                    name: layouts["vm_area_struct"]["member_sizes"][name]
                    for name in USER_PTMAP_VMA_MEMBER_KEYS
                },
            },
            "pt_regs": {
                "size": layouts["pt_regs"]["size"],
                "members": {
                    name: layouts["pt_regs"]["members"][name]
                    for name in USER_PTMAP_REGS_MEMBER_KEYS
                },
                "member_sizes": {
                    name: layouts["pt_regs"]["member_sizes"][name]
                    for name in USER_PTMAP_REGS_MEMBER_KEYS
                },
            },
        }
    except (KeyError, TypeError) as error:
        raise ValueError(
            f"{context}: user_ptmap lacks member width evidence"
        ) from error
    validate_user_ptmap_layout(
        normalized, context, normalized["geometry"]
    )
    return normalized


def arm64_task_stack_size(manifest):
    """Return THREAD_SIZE for the certified non-KASAN ARM64 GKI profile."""
    page_shift = manifest["config"]["PAGE_SHIFT"]
    if page_shift not in (12, 14, 16):
        raise ValueError("manifest has unsupported ARM64 PAGE_SHIFT")
    return 1 << max(page_shift, 14)


def compile_runtime_layout(manifest):
    """Flatten every field emitted into struct neverc_krt_gki_layout."""
    layouts = manifest["layouts"]
    runtime_layout = {
        output_name: layout_value(layouts, structure, field)
        for output_name, structure, field in LAYOUT_FIELDS
    }
    user_geometry = normalize_user_ptmap_layout(
        manifest, f"profile {manifest.get('profile')}"
    )["geometry"]
    runtime_layout.update({
        output_name: user_geometry[geometry_name]
        for output_name, geometry_name in RUNTIME_LAYOUT_USER_GEOMETRY_FIELDS
    })
    runtime_layout.update(dict(RUNTIME_LAYOUT_FIXED_FIELDS))

    inode_times = normalize_inode_times_layout(
        layouts, f"profile {manifest.get('profile')}"
    )
    for output_name, container, member_name in RUNTIME_LAYOUT_INODE_FIELDS:
        if inode_times is None:
            runtime_layout[output_name] = 0
        elif container is None:
            runtime_layout[output_name] = inode_times["size"]
        else:
            runtime_layout[output_name] = inode_times[container][member_name]
    runtime_layout["task_stack_size"] = arm64_task_stack_size(manifest)

    expected = set(RUNTIME_LAYOUT_FIELD_NAMES)
    actual = set(runtime_layout)
    if actual != expected:
        missing = ", ".join(sorted(expected - actual)) or "-"
        extra = ", ".join(sorted(actual - expected)) or "-"
        raise ValueError(
            "runtime layout projection drift: "
            f"missing [{missing}], extra [{extra}]"
        )
    return runtime_layout


def require_u32(record, key, context, *, allow_zero=True):
    value = record.get(key)
    lower_bound = 0 if allow_zero else 1
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < lower_bound
        or value > UINT32_MAX
    ):
        requirement = "uint32" if allow_zero else "non-zero uint32"
        raise ValueError(f"{context}: {key} must be a {requirement}")
    return value


def load_catalog(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != 1:
        raise ValueError(f"{path}: unsupported schema")
    records = document.get("profiles")
    if not isinstance(records, list) or not records:
        raise ValueError(f"{path}: profiles must be a non-empty array")

    profiles = []
    seen_ids = set()
    seen_symbols = set()
    seen_identities = set()
    for index, original in enumerate(records):
        context = f"{path}: profiles[{index}]"
        keys = set(original) if isinstance(original, dict) else set()
        if (
            not isinstance(original, dict)
            or not PROFILE_KEYS.issubset(keys)
            or not keys.issubset(PROFILE_KEYS | OPTIONAL_PROFILE_KEYS)
        ):
            raise ValueError(f"{context}: profile keys do not match schema")
        profile = dict(original)
        legacy_id = require_u32(
            profile, "legacy_id", context, allow_zero=False
        )
        symbol = profile.get("symbol")
        if not isinstance(symbol, str) or not SYMBOL_NAME.fullmatch(symbol):
            raise ValueError(f"{context}: invalid symbolic profile name")
        if legacy_id in seen_ids:
            raise ValueError(f"{context}: duplicate legacy_id {legacy_id}")
        if symbol in seen_symbols:
            raise ValueError(f"{context}: duplicate symbol {symbol}")
        seen_ids.add(legacy_id)
        seen_symbols.add(symbol)
        aliases = profile.get("aliases", [])
        if aliases:
            if not isinstance(aliases, list) or not aliases:
                raise ValueError(f"{context}: aliases must be a non-empty array")
            parsed_aliases = []
            for alias_index, alias in enumerate(aliases):
                alias_context = f"{context}: aliases[{alias_index}]"
                if (
                    not isinstance(alias, int)
                    or isinstance(alias, bool)
                    or alias <= 0
                    or alias > UINT32_MAX
                ):
                    raise ValueError(f"{alias_context}: invalid alias")
                if alias in seen_ids:
                    raise ValueError(f"{alias_context}: duplicate id {alias}")
                seen_ids.add(alias)
                parsed_aliases.append(alias)
            profile["aliases"] = parsed_aliases
        else:
            profile["aliases"] = []
        vermagic = profile.get("vermagic")
        if vermagic is not None:
            if not isinstance(vermagic, str) or not vermagic:
                raise ValueError(f"{context}: invalid vermagic")
            match = VERMAGIC_IDENTITY.match(vermagic)
            identity = (
                profile["linux_major"],
                profile["linux_minor"],
                profile["linux_patch"],
                profile["android_release"],
                profile["kmi_generation"],
            )
            if match is None or tuple(
                int(match.group(name))
                for name in ("major", "minor", "patch", "android", "kmi")
            ) != identity:
                raise ValueError(f"{context}: vermagic identity mismatch")

        for key in (
            "linux_major",
            "linux_minor",
            "linux_patch",
            "android_release",
            "kmi_generation",
            "page_shift",
        ):
            require_u32(profile, key, context)
        if profile["page_shift"] not in (12, 14, 16):
            raise ValueError(f"{context}: unsupported page_shift")
        identity = (
            profile["linux_major"],
            profile["linux_minor"],
            profile["linux_patch"],
            profile["android_release"],
            profile["kmi_generation"],
            profile["page_shift"],
        )
        if identity in seen_identities:
            raise ValueError(f"{context}: duplicate semantic identity")
        seen_identities.add(identity)

        kernel_name = profile.get("kernel_name")
        expected_kernel_name = (
            f"android{profile['android_release']}-"
            f"{profile['linux_major']}.{profile['linux_minor']}"
        )
        if kernel_name != expected_kernel_name:
            raise ValueError(
                f"{context}: kernel_name must be {expected_kernel_name}"
            )
        try:
            kimage_vaddr = int(profile["kimage_vaddr"], 0)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{context}: invalid kimage_vaddr") from error
        if kimage_vaddr < 0 or kimage_vaddr > 0xFFFFFFFFFFFFFFFF:
            raise ValueError(f"{context}: kimage_vaddr is not uint64")
        profile["kimage_vaddr_value"] = kimage_vaddr

        if profile.get("kcfi_mode") not in KCFI_MODES:
            raise ValueError(f"{context}: invalid kcfi_mode")
        if profile.get("shadow_call_stack_mode") not in SHADOW_CALL_STACK_MODES:
            raise ValueError(f"{context}: invalid shadow_call_stack_mode")
        capabilities = profile.get("capabilities")
        if not isinstance(capabilities, dict) or set(capabilities) != CAPABILITY_KEYS:
            raise ValueError(f"{context}: capability keys do not match schema")
        for key, values in CAPABILITY_ABIS.items():
            if capabilities[key] not in values:
                raise ValueError(f"{context}: invalid {key}")
        if not isinstance(capabilities["ftrace_registration_api"], bool):
            raise ValueError(f"{context}: ftrace_registration_api must be boolean")
        if (
            capabilities["ftrace_registration_api"]
            and capabilities["ftrace_callback_abi"] == "ftrace_regs"
        ):
            raise ValueError(
                f"{context}: ftrace_regs registration requires a modeled "
                "ftrace_regs-to-pt_regs layout"
            )
        profiles.append(profile)

    return sorted(profiles, key=lambda profile: profile["legacy_id"])


def validate_evidence(profiles, manifest_root, release_lock_path):
    catalog_ids = {profile["legacy_id"] for profile in profiles}
    manifest_paths = {int(path.stem): path for path in manifest_root.glob("*.json")}
    release_document = json.loads(release_lock_path.read_text(encoding="utf-8"))
    release_profiles = release_document.get("profiles", {})
    try:
        release_ids = {int(profile_id) for profile_id in release_profiles}
    except ValueError as error:
        raise ValueError(f"{release_lock_path}: non-numeric profile key") from error

    if set(manifest_paths) != catalog_ids:
        raise ValueError("catalog and manifest profile sets differ")
    if not release_ids.issubset(catalog_ids):
        raise ValueError("release-lock has profiles missing from the catalog")

    result = []
    for profile in profiles:
        legacy_id = profile["legacy_id"]
        manifest_path = manifest_paths[legacy_id]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if str(legacy_id) in release_profiles:
            release = dict(release_profiles[str(legacy_id)])
        else:
            vermagic = profile.get("vermagic")
            if not isinstance(vermagic, str) or not vermagic:
                raise ValueError(
                    f"profile {legacy_id} needs a catalog vermagic or a "
                    "release-lock entry"
                )
            release = {
                "kernel_name": profile["kernel_name"],
                "vermagic": vermagic,
                "kcfi_typeids": EXPECTED_KCFI_TYPEIDS[profile["kcfi_mode"]],
            }
        if manifest.get("profile") != legacy_id:
            raise ValueError(f"{manifest_path}: profile mismatch")
        if manifest.get("kernel_name") != profile["kernel_name"]:
            raise ValueError(f"{manifest_path}: kernel_name mismatch")
        if manifest.get("config", {}).get("PAGE_SHIFT") != profile["page_shift"]:
            raise ValueError(f"{manifest_path}: page_shift mismatch")
        layouts = manifest.get("layouts", {})
        validate_dir_context_layout(layouts, manifest_path)
        normalize_filename_name_layout(layouts, manifest_path)
        normalize_path_inode_layout(layouts, manifest_path)
        normalize_inode_times_layout(layouts, manifest_path)
        normalize_user_ptmap_layout(manifest, manifest_path)
        if release.get("kernel_name") != profile["kernel_name"]:
            raise ValueError(
                f"{release_lock_path}: profile {legacy_id} kernel_name mismatch"
            )
        vermagic = release.get("vermagic")
        if not isinstance(vermagic, str) or not vermagic:
            raise ValueError(
                f"{release_lock_path}: profile {legacy_id} invalid vermagic"
            )
        match = VERMAGIC_IDENTITY.match(vermagic)
        identity = (
            profile["linux_major"],
            profile["linux_minor"],
            profile["linux_patch"],
            profile["android_release"],
            profile["kmi_generation"],
        )
        if match is None or tuple(
            int(match.group(name))
            for name in ("major", "minor", "patch", "android", "kmi")
        ) != identity:
            raise ValueError(
                f"{release_lock_path}: profile {legacy_id} vermagic identity mismatch"
            )
        expected_typeids = EXPECTED_KCFI_TYPEIDS[profile["kcfi_mode"]]
        actual_typeids = release.get("kcfi_typeids")
        if actual_typeids != expected_typeids:
            raise ValueError(
                f"{release_lock_path}: profile {legacy_id} KCFI evidence mismatch"
            )
        release["release_token"] = vermagic.split(None, 1)[0]
        result.append((profile, manifest, release))
    return result


def load_layout_certificates(path, profile_evidence):
    """Validate exact per-field compatibility evidence against its family."""
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or set(document) != {
        "schema", "certificates"
    }:
        raise ValueError(f"{path}: certificate document keys do not match schema")
    document_schema = document.get("schema")
    if document_schema not in (1, 2):
        raise ValueError(f"{path}: unsupported certificate schema")
    records = document.get("certificates")
    if not isinstance(records, list) or not records:
        raise ValueError(f"{path}: certificates must be a non-empty array")

    families = {
        profile["legacy_id"]: (profile, manifest)
        for profile, manifest, _ in profile_evidence
    }
    certificates = []
    seen_identities = set()
    for index, original in enumerate(records):
        context = f"{path}: certificates[{index}]"
        keys = set(original) if isinstance(original, dict) else set()
        evidence_keys = keys.intersection(LAYOUT_CERTIFICATE_EVIDENCE_KEYS)
        if (
            not isinstance(original, dict)
            or not LAYOUT_CERTIFICATE_BASE_KEYS.issubset(keys)
            or not keys.issubset(
                LAYOUT_CERTIFICATE_BASE_KEYS
                | LAYOUT_CERTIFICATE_EVIDENCE_KEYS
                | LAYOUT_CERTIFICATE_FIELD_KEYS
                | LAYOUT_CERTIFICATE_FULL_KEYS
            )
            or len(evidence_keys) != 1
            or not (
                "runtime_layout" in keys
                or keys.intersection({
                    "dir_context",
                    "filename_name",
                    "inode_times",
                    "path_inode",
                    "task_ref",
                    "task_threads",
                    "task_user_state",
                    "task_walk",
                    "user_ptmap",
                    "file_dentry",
                })
            )
            or (("dir_context" in keys) != ("filldir_abi" in keys))
            or (("runtime_layout" in keys) != ("manifest_evidence" in keys))
            or ("runtime_layout" in keys and document_schema != 2)
        ):
            raise ValueError(f"{context}: certificate keys do not match schema")
        certificate = dict(original)
        profile_id = require_u32(
            certificate, "profile_id", context, allow_zero=False
        )
        if profile_id not in families:
            raise ValueError(f"{context}: unknown profile family {profile_id}")
        profile, manifest = families[profile_id]

        identity = certificate.get("identity")
        if (
            not isinstance(identity, dict)
            or set(identity) != LAYOUT_CERTIFICATE_IDENTITY_KEYS
        ):
            raise ValueError(f"{context}: identity keys do not match schema")
        for key in LAYOUT_CERTIFICATE_IDENTITY_KEYS:
            require_u32(identity, key, f"{context}: identity")
        if identity["page_shift"] not in (12, 14, 16):
            raise ValueError(f"{context}: unsupported certificate page_shift")

        release_token = certificate.get("release_token")
        if (
            not isinstance(release_token, str)
            or not release_token
            or any(ord(char) < 0x21 or ord(char) > 0x7e for char in release_token)
        ):
            raise ValueError(f"{context}: invalid release_token")
        token_match = RELEASE_TOKEN_IDENTITY.fullmatch(release_token)
        token_identity = (
            identity["linux_major"],
            identity["linux_minor"],
            identity["linux_patch"],
            identity["android_release"],
            identity["kmi_generation"],
        )
        if token_match is None or tuple(
            int(token_match.group(name))
            for name in ("major", "minor", "patch", "android", "kmi")
        ) != token_identity:
            raise ValueError(f"{context}: release_token identity mismatch")

        family_identity = (
            profile["linux_major"],
            profile["linux_minor"],
            profile["page_shift"],
        )
        certificate_family = (
            identity["linux_major"],
            identity["linux_minor"],
            identity["page_shift"],
        )
        if certificate_family != family_identity:
            raise ValueError(f"{context}: identity is outside profile family")

        evidence_name = next(iter(evidence_keys))
        evidence = certificate.get(evidence_name)
        if not isinstance(evidence, dict) or set(evidence) != {"sha256", "size"}:
            raise ValueError(
                f"{context}: {evidence_name} keys do not match schema"
            )
        evidence_hash = evidence.get("sha256")
        evidence_size = evidence.get("size")
        if (
            not isinstance(evidence_hash, str)
            or not SHA256_HEX.fullmatch(evidence_hash)
        ):
            raise ValueError(
                f"{context}: {evidence_name}.sha256 must be lowercase SHA-256"
            )
        if (
            not isinstance(evidence_size, int)
            or isinstance(evidence_size, bool)
            or evidence_size <= 0
            or evidence_size > UINT64_MAX
        ):
            raise ValueError(
                f"{context}: {evidence_name}.size must be a positive uint64"
            )

        if "runtime_layout" in certificate:
            runtime_layout = certificate["runtime_layout"]
            if (
                not isinstance(runtime_layout, dict)
                or set(runtime_layout) != {"schema", "fields"}
                or runtime_layout.get("schema") != 1
                or not isinstance(runtime_layout.get("fields"), dict)
                or set(runtime_layout["fields"])
                != set(RUNTIME_LAYOUT_FIELD_NAMES)
            ):
                raise ValueError(
                    f"{context}: runtime_layout keys do not match schema"
                )
            for field_name in RUNTIME_LAYOUT_FIELD_NAMES:
                value = runtime_layout["fields"][field_name]
                if (
                    not isinstance(value, int)
                    or isinstance(value, bool)
                    or value < 0
                    or value > UINT64_MAX
                ):
                    raise ValueError(
                        f"{context}: runtime_layout.{field_name} "
                        "must be a uint64"
                    )
            manifest_evidence = certificate["manifest_evidence"]
            if (
                not isinstance(manifest_evidence, dict)
                or set(manifest_evidence)
                != LAYOUT_CERTIFICATE_MANIFEST_EVIDENCE_KEYS
                or not SHA256_HEX.fullmatch(
                    str(manifest_evidence.get("config_sha256", ""))
                )
                or not SHA256_HEX.fullmatch(
                    str(manifest_evidence.get("layout_sha256", ""))
                )
                or not isinstance(
                    manifest_evidence.get("vmlinux_build_id"), str
                )
                or not manifest_evidence["vmlinux_build_id"]
                or manifest_evidence["layout_sha256"] != evidence_hash
            ):
                raise ValueError(
                    f"{context}: manifest_evidence is not target-bound"
                )
            expected_loader = compile_loader_abi_contract(manifest)
            observed_loader = {
                name: runtime_layout["fields"][name]
                for name in LOADER_ABI_FIELD_NAMES
            }
            if observed_loader != expected_loader:
                raise ValueError(
                    f"{context}: runtime_layout loader ABI mismatches "
                    "profile family"
                )

        if "dir_context" in certificate:
            dir_context = certificate["dir_context"]
            if (
                not isinstance(dir_context, dict)
                or set(dir_context) != DIR_CONTEXT_LAYOUT_KEYS
                or not isinstance(dir_context.get("members"), dict)
                or set(dir_context["members"]) != DIR_CONTEXT_MEMBER_KEYS
                or not isinstance(dir_context.get("member_sizes"), dict)
                or set(dir_context["member_sizes"]) != DIR_CONTEXT_MEMBER_KEYS
            ):
                raise ValueError(f"{context}: dir_context keys do not match schema")
            validate_dir_context_layout({"dir_context": dir_context}, context)

            filldir_abi = certificate["filldir_abi"]
            if filldir_abi not in CAPABILITY_ABIS["filldir_abi"]:
                raise ValueError(f"{context}: invalid filldir_abi")
            if filldir_abi != profile["capabilities"]["filldir_abi"]:
                raise ValueError(
                    f"{context}: filldir_abi mismatches profile family"
                )

        if "filename_name" in certificate:
            validate_filename_name_layout(
                certificate["filename_name"], context
            )
        if "inode_times" in certificate:
            validate_inode_times_layout(certificate["inode_times"], context)
        if "path_inode" in certificate:
            validate_path_inode_layout(certificate["path_inode"], context)
        if "task_walk" in certificate:
            validate_task_walk_layout(certificate["task_walk"], context)
        if "task_ref" in certificate:
            validate_task_ref_layout(certificate["task_ref"], context)
        if "task_user_state" in certificate:
            validate_task_user_state_layout(
                certificate["task_user_state"], context
            )
        if "task_threads" in certificate:
            validate_task_threads_layout(certificate["task_threads"], context)
        if "user_ptmap" in certificate:
            expected_user_ptmap = normalize_user_ptmap_layout(
                manifest, context
            )
            validate_user_ptmap_layout(
                certificate["user_ptmap"],
                context,
                expected_user_ptmap["geometry"],
            )
        if "file_dentry" in certificate:
            file_dentry = certificate["file_dentry"]
            if (
                not isinstance(file_dentry, int)
                or isinstance(file_dentry, bool)
                or file_dentry < 0
                or file_dentry > UINT64_MAX
            ):
                raise ValueError(f"{context}: file_dentry must be a uint64")

        task_sizes = {
            certificate[field]["task_struct"]["size"]
            for field in (
                "task_ref", "task_threads", "task_user_state", "task_walk"
            )
            if field in certificate
        }
        if len(task_sizes) > 1:
            raise ValueError(
                f"{context}: task private certificate sizes disagree"
            )

        if "task_user_state" in certificate and "user_ptmap" in certificate:
            user_state_regs = certificate["task_user_state"]["pt_regs"]
            user_ptmap_regs = certificate["user_ptmap"]["pt_regs"]
            shared_members = ("pc", "pstate")
            if (
                user_state_regs["size"] != user_ptmap_regs["size"]
                or any(
                    user_state_regs["members"][member]
                    != user_ptmap_regs["members"][member]
                    or user_state_regs["member_sizes"][member]
                    != user_ptmap_regs["member_sizes"][member]
                    for member in shared_members
                )
            ):
                raise ValueError(
                    f"{context}: pt_regs certificate views disagree"
                )

        owner = dedicated_compile_family(
            (family for family, _ in families.values()),
            identity,
        )
        if owner is not None and owner != profile_id:
            raise ValueError(
                f"{context}: {release_token} is compile family {owner}, "
                f"not a leftover overlay on {profile_id}"
            )
        identity_key = (profile_id, release_token)
        if identity_key in seen_identities:
            raise ValueError(f"{context}: duplicate certificate identity")
        seen_identities.add(identity_key)
        certificates.append(certificate)

    return sorted(
        certificates,
        key=lambda certificate: (
            certificate["profile_id"],
            certificate["release_token"],
        ),
    )


def compile_layout_contract(manifest):
    """Return the SDK-facing facts verified by a profile's layout manifest."""
    layouts = manifest["layouts"]
    config = manifest["config"]
    thread_info_members = layouts["thread_info"]["members"]
    return {
        "nr_cpus": config["CONFIG_NR_CPUS"],
        "page_shift": config["PAGE_SHIFT"],
        "va_bits": config["CONFIG_ARM64_VA_BITS"],
        "pa_bits": config["CONFIG_ARM64_PA_BITS"],
        "pgtable_levels": config["CONFIG_PGTABLE_LEVELS"],
        "module_size": layouts["module"]["size"],
        "module_list": member(layouts, "module", "list"),
        "module_name": member(layouts, "module", "name"),
        "module_init": member(layouts, "module", "init"),
        "module_exit": member(layouts, "module", "exit"),
        "file_dentry": (
            member(layouts, "file", "f_path")
            + member(layouts, "path", "dentry")
        ),
        "dentry_name": (
            member(layouts, "dentry", "d_name")
            + member(layouts, "qstr", "name")
        ),
        "sock_dport": member(layouts, "sock_common", "skc_dport"),
        "sock_num": member(layouts, "sock_common", "skc_num"),
        "file_operations_size": layouts["file_operations"]["size"],
        "task_preempt_count": member(
            layouts, "thread_info", "preempt_count"
        ),
        "has_task_cpu": "cpu" in thread_info_members,
        "task_cpu": thread_info_members.get("cpu", 0),
    }


LOADER_ABI_FIELD_NAMES = (
    "module_size",
    "module_list",
    "module_name",
    "module_init",
    "module_exit",
)


def compile_loader_abi_contract(manifest):
    """Return fields the kernel consumes before module init can run."""
    contract = compile_layout_contract(manifest)
    return {name: contract[name] for name in LOADER_ABI_FIELD_NAMES}


def abi_config_symbol(field, semantic_name):
    return CAPABILITY_ABIS[field][semantic_name][0]


def abi_runtime_symbol(field, semantic_name):
    return CAPABILITY_ABIS[field][semantic_name][1]


def render_profile_ids_header(profiles):
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0 */",
        "/* Generated by tools/generate-compat-table.py.  Do not edit. */",
        "#ifndef NEVERC_KRT_PROFILE_IDS_H",
        "#define NEVERC_KRT_PROFILE_IDS_H",
        "",
    ]
    for _, (symbol, value) in KCFI_MODES.items():
        lines.append(f"#define {symbol} {value}")
    for _, (symbol, value) in SHADOW_CALL_STACK_MODES.items():
        lines.append(f"#define {symbol} {value}")
    lines.append("")
    for values in CAPABILITY_ABIS.values():
        for _, (symbol, _, value) in values.items():
            lines.append(f"#define {symbol} {value}")
    lines.append("")
    for profile in profiles:
        lines.append(
            f"#define NEVERC_KRT_PROFILE_{profile['symbol']} "
            f"{profile['legacy_id']}"
        )
        for alias in profile.get("aliases", []):
            lines.append(
                f"#define NEVERC_KRT_PROFILE_{profile['symbol']}_ALIAS "
                f"{alias}"
            )
    remaps = [
        (alias, profile["legacy_id"])
        for profile in profiles
        for alias in profile.get("aliases", [])
    ]
    lines.extend([
        "",
        "#endif /* NEVERC_KRT_PROFILE_IDS_H */",
        "",
    ])
    if remaps:
        lines.append("/* Re-entrant: alias spellings must remap on a later include. */")
        for alias, canonical in remaps:
            lines.extend([
                f"#if defined(NVK_KERNEL) && (NVK_KERNEL == {alias})",
                "#  undef NVK_KERNEL",
                f"#  define NVK_KERNEL {canonical}",
                "#endif",
                f"#if defined(NEVERC_KRT_KERNEL) && (NEVERC_KRT_KERNEL == {alias})",
                "#  undef NEVERC_KRT_KERNEL",
                f"#  define NEVERC_KRT_KERNEL {canonical}",
                "#endif",
            ])
        lines.append("")
    return "\n".join(lines)


def render_profile_header(profile_evidence):
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0 */",
        "/* Generated by tools/generate-compat-table.py.  Do not edit. */",
        "/* Deliberately re-entrant: an early include before profile selection",
        " * must not suppress a later configured inclusion. */",
        "",
        "#include <nvk_profile_ids.h>",
        "",
    ]
    lines.append(
        "#if defined(NEVERC_KRT_KERNEL) && "
        "!defined(NEVERC_KRT_PROFILE_CONFIGURED)"
    )

    for index, (profile, manifest, release) in enumerate(profile_evidence):
        directive = "#  if" if index == 0 else "#  elif"
        caps = profile["capabilities"]
        layout = compile_layout_contract(manifest)
        lines.extend([
            f"{directive} NEVERC_KRT_KERNEL == "
            f"NEVERC_KRT_PROFILE_{profile['symbol']}",
            f"#    define NEVERC_KRT_PROFILE_ID "
            f"NEVERC_KRT_PROFILE_{profile['symbol']}",
            f"#    define NEVERC_KRT_PROFILE_LINUX_MAJOR "
            f"{profile['linux_major']}",
            f"#    define NEVERC_KRT_PROFILE_LINUX_MINOR "
            f"{profile['linux_minor']}",
            f"#    define NEVERC_KRT_PROFILE_LINUX_PATCH "
            f"{profile['linux_patch']}",
            f"#    define NEVERC_KRT_PROFILE_ANDROID_RELEASE "
            f"{profile['android_release']}",
            f"#    define NEVERC_KRT_PROFILE_KMI_GENERATION "
            f"{profile['kmi_generation']}",
            f"#    define NEVERC_KRT_PROFILE_PAGE_SHIFT "
            f"{profile['page_shift']}",
            f"#    define NEVERC_KRT_PROFILE_KIMAGE_VADDR "
            f"0x{profile['kimage_vaddr_value']:016X}UL",
            f"#    define NEVERC_KRT_PROFILE_KCFI_MODE "
            f"{KCFI_MODES[profile['kcfi_mode']][0]}",
            f"#    define NEVERC_KRT_PROFILE_SCS_MODE "
            f"{SHADOW_CALL_STACK_MODES[profile['shadow_call_stack_mode']][0]}",
            f"#    define NEVERC_KRT_PROFILE_FTRACE_CALLBACK_ABI "
            f"{abi_config_symbol('ftrace_callback_abi', caps['ftrace_callback_abi'])}",
            f"#    define NEVERC_KRT_PROFILE_FILLDIR_ABI "
            f"{abi_config_symbol('filldir_abi', caps['filldir_abi'])}",
            f"#    define NEVERC_KRT_PROFILE_KALLSYMS_ITER_ABI "
            f"{abi_config_symbol('kallsyms_iter_abi', caps['kallsyms_iter_abi'])}",
            f"#    define NEVERC_KRT_PROFILE_DO_MMAP_ABI "
            f"{abi_config_symbol('do_mmap_abi', caps['do_mmap_abi'])}",
            f"#    define NEVERC_KRT_PROFILE_HAS_FTRACE_REGISTRATION_API "
            f"{int(caps['ftrace_registration_api'])}",
            f"#    define NEVERC_KRT_PROFILE_VERMAGIC "
            f"{json.dumps(release['vermagic'])}",
            f"#    define NEVERC_KRT_PROFILE_KERNEL_NAME "
            f"{json.dumps(profile['kernel_name'])}",
            f"#    define NEVERC_KRT_PROFILE_MODULE_INIT_OFFSET "
            f"{layout['module_init']}",
            f"#    define NEVERC_KRT_PROFILE_MODULE_EXIT_OFFSET "
            f"{layout['module_exit']}",
            f"#    define NEVERC_KRT_PROFILE_MODULE_SIZE "
            f"{layout['module_size']}",
            f"#    define NEVERC_KRT_PROFILE_MODULE_LIST_OFFSET "
            f"{layout['module_list']}",
            f"#    define NEVERC_KRT_PROFILE_MODULE_NAME_OFFSET "
            f"{layout['module_name']}",
            f"#    define NEVERC_KRT_PROFILE_FILE_DENTRY_OFFSET "
            f"{layout['file_dentry']}",
            f"#    define NEVERC_KRT_PROFILE_DENTRY_NAME_OFFSET "
            f"{layout['dentry_name']}",
            f"#    define NEVERC_KRT_PROFILE_SOCK_DPORT_OFFSET "
            f"{layout['sock_dport']}",
            f"#    define NEVERC_KRT_PROFILE_SOCK_NUM_OFFSET "
            f"{layout['sock_num']}",
            f"#    define NEVERC_KRT_PROFILE_FILE_OPERATIONS_SIZE "
            f"{layout['file_operations_size']}",
            f"#    define NEVERC_KRT_PROFILE_TASK_PREEMPT_COUNT_OFFSET "
            f"{layout['task_preempt_count']}",
            f"#    define NEVERC_KRT_PROFILE_HAS_TASK_CPU_OFFSET "
            f"{int(layout['has_task_cpu'])}",
            f"#    define NEVERC_KRT_PROFILE_TASK_CPU_OFFSET "
            f"{layout['task_cpu']}",
            f"#    define NEVERC_KRT_PROFILE_NR_CPUS "
            f"{layout['nr_cpus']}",
            f"#    define NEVERC_KRT_PROFILE_VA_BITS "
            f"{layout['va_bits']}",
            f"#    define NEVERC_KRT_PROFILE_PA_BITS "
            f"{layout['pa_bits']}",
            f"#    define NEVERC_KRT_PROFILE_PGTABLE_LEVELS "
            f"{layout['pgtable_levels']}",
        ])
    lines.extend([
        "#  else",
        "#    error \"Unsupported NEVERC_KRT_KERNEL profile; use a generated "
        "NEVERC_KRT_PROFILE_* ID\"",
        "#  endif",
        "#  define NEVERC_KRT_PROFILE_CONFIGURED 1",
        "#endif /* selected but not configured */",
        "",
        "#if defined(NEVERC_KRT_KERNEL) && "
        "defined(NEVERC_KRT_PROFILE_CONFIGURED)",
        "#  if NEVERC_KRT_PROFILE_ID != NEVERC_KRT_KERNEL",
        "#    error \"Android kernel profile changed after configuration\"",
        "#  endif",
        "#endif /* configured profile validation */",
        "",
    ])
    lines.append(
        "#if defined(NEVERC_KRT_PROFILE_CONFIGURED) && "
        "!defined(NEVERC_KRT_PROFILE_PUBLIC_ALIASES_DEFINED)"
    )
    for public_name, profile_name in PROFILE_STRING_ALIASES:
        lines.extend([
            f"#  ifdef {public_name}",
            f"#    error \"{public_name} override requires a generated profile\"",
            "#  endif",
            f"#  define {public_name} {profile_name}",
        ])
    for public_name, profile_name in PROFILE_NUMERIC_ALIASES:
        lines.extend([
            f"#  if defined({public_name}) && {public_name} != {profile_name}",
            f"#    error \"{public_name} conflicts with selected profile\"",
            "#  endif",
            f"#  ifndef {public_name}",
            f"#    define {public_name} {profile_name}",
            "#  endif",
        ])
    lines.extend([
        "#  define NEVERC_KRT_PROFILE_PUBLIC_ALIASES_DEFINED 1",
        "#endif /* exact public aliases */",
        "",
    ])
    return "\n".join(lines)


def render_profile_table(profile_evidence):
    remaps = [
        (alias, profile["legacy_id"])
        for profile, _, _ in profile_evidence
        for alias in profile.get("aliases", [])
    ]
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0 */",
        "/* Generated by tools/generate-compat-table.py.  Do not edit. */",
        "static unsigned int neverc_krt_canonical_legacy_id("
        "unsigned int legacy_id)",
        "{",
        "\tswitch (legacy_id) {",
    ]
    for alias, canonical in remaps:
        lines.append(f"\tcase {alias}:")
        lines.append(f"\t\treturn {canonical};")
    lines.extend([
        "\tdefault:",
        "\t\treturn legacy_id;",
        "\t}",
        "}",
        "",
        f"#define NEVERC_KRT_PROFILE_COUNT {len(profile_evidence)}UL",
        "",
        "static const struct neverc_krt_profile",
        "_neverc_krt_profiles[NEVERC_KRT_PROFILE_COUNT] = {",
    ])
    for profile, _, release in profile_evidence:
        caps = profile["capabilities"]
        lines.extend([
            "\t{",
            f"\t\t.legacy_id = {profile['legacy_id']},",
            f"\t\t.linux_major = {profile['linux_major']},",
            f"\t\t.linux_minor = {profile['linux_minor']},",
            f"\t\t.linux_patch = {profile['linux_patch']},",
            f"\t\t.android_release = {profile['android_release']},",
            f"\t\t.kmi_generation = {profile['kmi_generation']},",
            f"\t\t.page_shift = {profile['page_shift']},",
            f"\t\t.release_token = {json.dumps(release['release_token'])},",
            f"\t\t.kimage_vaddr = "
            f"0x{profile['kimage_vaddr_value']:016X}UL,",
            f"\t\t.kcfi_mode = {KCFI_MODES[profile['kcfi_mode']][0]},",
            "\t\t.caps = {",
            f"\t\t\t.ftrace_callback_abi = "
            f"{abi_runtime_symbol('ftrace_callback_abi', caps['ftrace_callback_abi'])},",
            f"\t\t\t.filldir_abi = "
            f"{abi_runtime_symbol('filldir_abi', caps['filldir_abi'])},",
            f"\t\t\t.kallsyms_iter_abi = "
            f"{abi_runtime_symbol('kallsyms_iter_abi', caps['kallsyms_iter_abi'])},",
            f"\t\t\t.do_mmap_abi = "
            f"{abi_runtime_symbol('do_mmap_abi', caps['do_mmap_abi'])},",
            f"\t\t\t.has_ftrace_registration_api = "
            f"{int(caps['ftrace_registration_api'])},",
            "\t\t},",
            "\t},",
        ])
    lines.extend(["};", ""])
    return "\n".join(lines)


RUNTIME_LAYOUT_HEX_FIELDS = frozenset({
    "user_descriptor_address_mask",
    "user_physical_address_mask",
    "user_physical_page_mask",
})


def render_runtime_layout_fields(runtime_layout, indent):
    """Render one complete neverc_krt_gki_layout initializer."""
    lines = []
    for field_name in RUNTIME_LAYOUT_FIELD_NAMES:
        value = runtime_layout[field_name]
        if field_name in RUNTIME_LAYOUT_HEX_FIELDS:
            rendered = f"0x{value:016x}UL"
        else:
            rendered = str(value)
        lines.append(f"{indent}.{field_name} = {rendered},")
    return lines


def render_compat_table(profile_evidence, layout_certificates):
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0 */",
        "/*",
        " * Generated by tools/generate-compat-table.py from the checked",
        " * profile catalog, manifests, and per-field certificates.",
        " * Do not edit by hand.",
        " */",
        "",
        "struct neverc_krt_layout_entry {",
        "\tunsigned int profile_id;",
        "\tunsigned long module_size;",
        "\tunsigned long file_dentry_off;",
        "\tunsigned long off_init;",
        "\tunsigned long off_exit;",
        "\tstruct neverc_krt_gki_layout layout;",
        "};",
        "",
        f"#define NEVERC_KRT_LAYOUT_COUNT {len(profile_evidence)}UL",
        "",
        "static const struct neverc_krt_layout_entry",
        "_neverc_krt_layouts[NEVERC_KRT_LAYOUT_COUNT] = {",
    ]

    for profile, manifest, _ in profile_evidence:
        layouts = manifest["layouts"]
        runtime_layout = compile_runtime_layout(manifest)
        lines.extend([
            "\t{",
            f"\t\t.profile_id = {profile['legacy_id']},",
            f"\t\t.module_size = {layouts['module']['size']},",
            f"\t\t.file_dentry_off = {runtime_layout['file_dentry']},",
            f"\t\t.off_init = {member(layouts, 'module', 'init')},",
            f"\t\t.off_exit = {member(layouts, 'module', 'exit')},",
            "\t\t.layout = {",
        ])
        lines.extend(render_runtime_layout_fields(runtime_layout, "\t\t\t"))
        lines.extend(["\t\t},", "\t},"])

    lines.extend(["};", ""])

    lines.extend([
        "struct neverc_krt_layout_certificate_entry {",
        "\tunsigned int profile_id;",
        "\tunsigned int linux_major;",
        "\tunsigned int linux_minor;",
        "\tunsigned int linux_patch;",
        "\tunsigned int android_release;",
        "\tunsigned int kmi_generation;",
        "\tunsigned int page_shift;",
        "\tconst char *release_token;",
        "\tunsigned long release_token_length;",
        "\tunsigned long field_bits;",
        "\tstruct neverc_krt_gki_layout runtime_layout;",
        "\tunsigned long dir_context_size;",
        "\tunsigned long dir_context_actor;",
        "\tunsigned long dir_context_actor_size;",
        "\tunsigned long dir_context_pos;",
        "\tunsigned long dir_context_pos_size;",
        "\tenum neverc_krt_filldir_abi filldir_abi;",
        "\tunsigned long filename_size;",
        "\tunsigned long filename_name;",
        "\tunsigned long filename_name_size;",
        "\tunsigned long path_size;",
        "\tunsigned long path_dentry;",
        "\tunsigned long path_dentry_size;",
        "\tunsigned long dentry_size;",
        "\tunsigned long dentry_inode;",
        "\tunsigned long dentry_inode_size;",
        "\tunsigned long inode_size;",
        "\tunsigned long inode_atime_sec;",
        "\tunsigned long inode_atime_sec_size;",
        "\tunsigned long inode_mtime_sec;",
        "\tunsigned long inode_mtime_sec_size;",
        "\tunsigned long inode_atime_nsec;",
        "\tunsigned long inode_atime_nsec_size;",
        "\tunsigned long inode_mtime_nsec;",
        "\tunsigned long inode_mtime_nsec_size;",
        "\tunsigned long task_walk_task_size;",
        "\tunsigned long task_ref_task_size;",
        "\tunsigned long task_user_state_task_size;",
        "\tunsigned long task_tasks;",
        "\tunsigned long task_usage;",
        "\tunsigned long task_stack;",
        "\tunsigned long task_stack_refcount;",
        "\tunsigned long task_flags;",
        "\tunsigned long task_mm;",
        "\tunsigned long task_parent;",
        "\tunsigned long task_real_parent;",
        "\tunsigned long task_group_leader;",
        "\tunsigned long task_real_cred;",
        "\tunsigned long task_comm;",
        "\tunsigned long cred_size;",
        "\tunsigned long cred_uid;",
        "\tunsigned long cred_gid;",
        "\tunsigned long cred_suid;",
        "\tunsigned long cred_sgid;",
        "\tunsigned long cred_euid;",
        "\tunsigned long cred_egid;",
        "\tunsigned long cred_fsuid;",
        "\tunsigned long cred_fsgid;",
        "\tunsigned long pt_regs_size;",
        "\tunsigned long pt_regs_regs;",
        "\tunsigned long pt_regs_regs_size;",
        "\tunsigned long pt_regs_sp;",
        "\tunsigned long pt_regs_sp_size;",
        "\tunsigned long pt_regs_pc;",
        "\tunsigned long pt_regs_pc_size;",
        "\tunsigned long pt_regs_pstate;",
        "\tunsigned long pt_regs_pstate_size;",
        "\tunsigned long mm_size;",
        "\tunsigned long mm_count;",
        "\tunsigned long mm_count_size;",
        "\tunsigned long mm_pgd;",
        "\tunsigned long mm_pgd_size;",
        "\tunsigned long mm_page_table_lock;",
        "\tunsigned long mm_page_table_lock_size;",
        "\tunsigned long mm_mmap_lock;",
        "\tunsigned long mm_mmap_lock_size;",
        "\tunsigned long vma_size;",
        "\tunsigned long vma_start;",
        "\tunsigned long vma_start_size;",
        "\tunsigned long vma_end;",
        "\tunsigned long vma_end_size;",
        "\tunsigned long vma_mm;",
        "\tunsigned long vma_mm_size;",
        "\tunsigned long vma_flags;",
        "\tunsigned long vma_flags_size;",
        "\tunsigned long vma_pgoff;",
        "\tunsigned long vma_pgoff_size;",
        "\tunsigned long user_page_shift;",
        "\tunsigned long user_va_bits;",
        "\tunsigned long user_pa_bits;",
        "\tunsigned long user_pgtable_levels;",
        "\tunsigned long user_pgd_shift;",
        "\tunsigned long user_pmd_shift;",
        "\tunsigned long user_pte_shift;",
        "\tunsigned long user_index_bits;",
        "\tunsigned long user_contiguous_bit;",
        "\tunsigned long user_contiguous_entries;",
        "\tunsigned long user_descriptor_address_mask;",
        "\tunsigned long user_physical_address_mask;",
        "\tunsigned long user_physical_page_mask;",
        "\tunsigned long user_tlbi_all_asid;",
        "\tunsigned long task_size;",
        "\tunsigned long task_pid;",
        "\tunsigned long task_thread_pid;",
        "\tunsigned long task_signal;",
        "\tunsigned long task_thread_node;",
        "\tunsigned long signal_size;",
        "\tunsigned long signal_thread_head;",
        "\tunsigned long file_dentry;",
        "};",
        "",
        f"#define NEVERC_KRT_LAYOUT_CERTIFICATE_COUNT "
        f"{len(layout_certificates)}UL",
        "",
        "static const struct neverc_krt_layout_certificate_entry",
        "_neverc_krt_layout_certificates[NEVERC_KRT_LAYOUT_CERTIFICATE_COUNT] = {",
    ])
    for certificate in layout_certificates:
        identity = certificate["identity"]
        if "raw_btf" in certificate:
            evidence_kind = "Raw BTF"
            evidence = certificate["raw_btf"]
        else:
            evidence_kind = "Raw DWARF"
            evidence = certificate["raw_dwarf"]
        field_bits = []
        if "runtime_layout" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_FULL")
        if "dir_context" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_DIR_CONTEXT")
        if "filename_name" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME")
        if "inode_times" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_INODE_TIMES")
        if "path_inode" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_PATH_INODE")
        if "task_walk" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_TASK_WALK")
        if "task_ref" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_TASK_REF")
        if "task_user_state" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_TASK_USER_STATE")
        if "task_threads" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_TASK_THREADS")
        if "user_ptmap" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_USER_PTMAP")
        if "file_dentry" in certificate:
            field_bits.append("NEVERC_KRT_LAYOUT_CERT_FILE_DENTRY")
        lines.extend([
            f"\t/* {evidence_kind} SHA-256 {evidence['sha256']}; "
            f"{evidence['size']} bytes. */",
            "\t{",
            f"\t\t.profile_id = {certificate['profile_id']},",
            f"\t\t.linux_major = {identity['linux_major']},",
            f"\t\t.linux_minor = {identity['linux_minor']},",
            f"\t\t.linux_patch = {identity['linux_patch']},",
            f"\t\t.android_release = {identity['android_release']},",
            f"\t\t.kmi_generation = {identity['kmi_generation']},",
            f"\t\t.page_shift = {identity['page_shift']},",
            f"\t\t.release_token = {json.dumps(certificate['release_token'])},",
            f"\t\t.release_token_length = "
            f"{len(certificate['release_token'])}UL,",
            f"\t\t.field_bits = {' | '.join(field_bits)},",
        ])
        if "runtime_layout" in certificate:
            lines.append("\t\t.runtime_layout = {")
            lines.extend(render_runtime_layout_fields(
                certificate["runtime_layout"]["fields"], "\t\t\t"
            ))
            lines.append("\t\t},")
        if "dir_context" in certificate:
            layout = certificate["dir_context"]
            lines.extend([
                f"\t\t.dir_context_size = {layout['size']},",
                f"\t\t.dir_context_actor = {layout['members']['actor']},",
                f"\t\t.dir_context_actor_size = "
                f"{layout['member_sizes']['actor']},",
                f"\t\t.dir_context_pos = {layout['members']['pos']},",
                f"\t\t.dir_context_pos_size = "
                f"{layout['member_sizes']['pos']},",
                f"\t\t.filldir_abi = "
                f"{abi_runtime_symbol('filldir_abi', certificate['filldir_abi'])},",
            ])
        if "filename_name" in certificate:
            filename = certificate["filename_name"]
            lines.extend([
                f"\t\t.filename_size = {filename['size']},",
                f"\t\t.filename_name = {filename['members']['name']},",
                f"\t\t.filename_name_size = "
                f"{filename['member_sizes']['name']},",
            ])
        if "path_inode" in certificate:
            path = certificate["path_inode"]["path"]
            dentry = certificate["path_inode"]["dentry"]
            lines.extend([
                f"\t\t.path_size = {path['size']},",
                f"\t\t.path_dentry = {path['members']['dentry']},",
                f"\t\t.path_dentry_size = {path['member_sizes']['dentry']},",
                f"\t\t.dentry_size = {dentry['size']},",
                f"\t\t.dentry_inode = {dentry['members']['d_inode']},",
                f"\t\t.dentry_inode_size = "
                f"{dentry['member_sizes']['d_inode']},",
            ])
        if "inode_times" in certificate:
            inode = certificate["inode_times"]
            lines.extend([
                f"\t\t.inode_size = {inode['size']},",
                f"\t\t.inode_atime_sec = {inode['members']['atime_sec']},",
                f"\t\t.inode_atime_sec_size = "
                f"{inode['member_sizes']['atime_sec']},",
                f"\t\t.inode_mtime_sec = {inode['members']['mtime_sec']},",
                f"\t\t.inode_mtime_sec_size = "
                f"{inode['member_sizes']['mtime_sec']},",
                f"\t\t.inode_atime_nsec = {inode['members']['atime_nsec']},",
                f"\t\t.inode_atime_nsec_size = "
                f"{inode['member_sizes']['atime_nsec']},",
                f"\t\t.inode_mtime_nsec = {inode['members']['mtime_nsec']},",
                f"\t\t.inode_mtime_nsec_size = "
                f"{inode['member_sizes']['mtime_nsec']},",
            ])
        if "task_walk" in certificate:
            walk = certificate["task_walk"]
            task = walk["task_struct"]
            cred = walk["cred"]
            lines.extend([
                f"\t\t.task_walk_task_size = {task['size']},",
                f"\t\t.task_tasks = {task['members']['tasks']},",
                f"\t\t.task_mm = {task['members']['mm']},",
                f"\t\t.task_parent = {task['members']['parent']},",
                f"\t\t.task_real_parent = "
                f"{task['members']['real_parent']},",
                f"\t\t.task_group_leader = "
                f"{task['members']['group_leader']},",
                f"\t\t.task_real_cred = {task['members']['real_cred']},",
                f"\t\t.task_comm = {task['members']['comm']},",
                f"\t\t.cred_size = {cred['size']},",
                f"\t\t.cred_uid = {cred['members']['uid']},",
                f"\t\t.cred_gid = {cred['members']['gid']},",
                f"\t\t.cred_suid = {cred['members']['suid']},",
                f"\t\t.cred_sgid = {cred['members']['sgid']},",
                f"\t\t.cred_euid = {cred['members']['euid']},",
                f"\t\t.cred_egid = {cred['members']['egid']},",
                f"\t\t.cred_fsuid = {cred['members']['fsuid']},",
                f"\t\t.cred_fsgid = {cred['members']['fsgid']},",
            ])
        if "task_ref" in certificate:
            task = certificate["task_ref"]["task_struct"]
            lines.extend([
                f"\t\t.task_ref_task_size = {task['size']},",
                f"\t\t.task_usage = {task['members']['usage']},",
            ])
        if "task_user_state" in certificate:
            state = certificate["task_user_state"]
            task = state["task_struct"]
            regs = state["pt_regs"]
            lines.extend([
                f"\t\t.task_user_state_task_size = {task['size']},",
                f"\t\t.task_stack = {task['members']['stack']},",
                f"\t\t.task_stack_refcount = "
                f"{task['members']['stack_refcount']},",
                f"\t\t.task_flags = {task['members']['flags']},",
                f"\t\t.pt_regs_size = {regs['size']},",
                f"\t\t.pt_regs_pc = {regs['members']['pc']},",
                f"\t\t.pt_regs_pstate = {regs['members']['pstate']},",
            ])
        if "task_threads" in certificate:
            task = certificate["task_threads"]["task_struct"]
            signal = certificate["task_threads"]["signal_struct"]
            lines.extend([
                f"\t\t.task_size = {task['size']},",
                f"\t\t.task_pid = {task['members']['pid']},",
                f"\t\t.task_thread_pid = {task['members']['thread_pid']},",
                f"\t\t.task_signal = {task['members']['signal']},",
                f"\t\t.task_thread_node = {task['members']['thread_node']},",
                f"\t\t.signal_size = {signal['size']},",
                f"\t\t.signal_thread_head = "
                f"{signal['members']['thread_head']},",
            ])
        if "user_ptmap" in certificate:
            ptmap = certificate["user_ptmap"]
            geometry = ptmap["geometry"]
            mm = ptmap["mm_struct"]
            vma = ptmap["vm_area_struct"]
            regs = ptmap["pt_regs"]
            lines.extend([
                f"\t\t.mm_size = {mm['size']},",
                f"\t\t.mm_count = {mm['members']['mm_count']},",
                f"\t\t.mm_count_size = "
                f"{mm['member_sizes']['mm_count']},",
                f"\t\t.mm_pgd = {mm['members']['pgd']},",
                f"\t\t.mm_pgd_size = {mm['member_sizes']['pgd']},",
                f"\t\t.mm_page_table_lock = "
                f"{mm['members']['page_table_lock']},",
                f"\t\t.mm_page_table_lock_size = "
                f"{mm['member_sizes']['page_table_lock']},",
                f"\t\t.mm_mmap_lock = {mm['members']['mmap_lock']},",
                f"\t\t.mm_mmap_lock_size = "
                f"{mm['member_sizes']['mmap_lock']},",
                f"\t\t.vma_size = {vma['size']},",
                f"\t\t.vma_start = {vma['members']['vm_start']},",
                f"\t\t.vma_start_size = "
                f"{vma['member_sizes']['vm_start']},",
                f"\t\t.vma_end = {vma['members']['vm_end']},",
                f"\t\t.vma_end_size = {vma['member_sizes']['vm_end']},",
            ])
            if USER_PTMAP_VMA_PUBLIC_KEYS.issubset(vma["members"]):
                lines.extend([
                    f"\t\t.vma_mm = {vma['members']['vm_mm']},",
                    f"\t\t.vma_mm_size = {vma['member_sizes']['vm_mm']},",
                    f"\t\t.vma_flags = {vma['members']['vm_flags']},",
                    f"\t\t.vma_flags_size = "
                    f"{vma['member_sizes']['vm_flags']},",
                    f"\t\t.vma_pgoff = {vma['members']['vm_pgoff']},",
                    f"\t\t.vma_pgoff_size = "
                    f"{vma['member_sizes']['vm_pgoff']},",
                ])
            lines.extend([
                f"\t\t.pt_regs_regs = {regs['members']['regs']},",
                f"\t\t.pt_regs_regs_size = "
                f"{regs['member_sizes']['regs']},",
                f"\t\t.pt_regs_sp = {regs['members']['sp']},",
                f"\t\t.pt_regs_sp_size = {regs['member_sizes']['sp']},",
                f"\t\t.pt_regs_pc_size = {regs['member_sizes']['pc']},",
                f"\t\t.pt_regs_pstate_size = "
                f"{regs['member_sizes']['pstate']},",
                f"\t\t.user_page_shift = {geometry['page_shift']},",
                f"\t\t.user_va_bits = {geometry['va_bits']},",
                f"\t\t.user_pa_bits = {geometry['pa_bits']},",
                f"\t\t.user_pgtable_levels = "
                f"{geometry['pgtable_levels']},",
                f"\t\t.user_pgd_shift = {geometry['pgd_shift']},",
                f"\t\t.user_pmd_shift = {geometry['pmd_shift']},",
                f"\t\t.user_pte_shift = {geometry['pte_shift']},",
                f"\t\t.user_index_bits = {geometry['index_bits']},",
                f"\t\t.user_contiguous_bit = "
                f"{geometry['contiguous_bit']},",
                f"\t\t.user_contiguous_entries = "
                f"{geometry['contiguous_entries']},",
                f"\t\t.user_descriptor_address_mask = "
                f"0x{geometry['descriptor_address_mask']:016x}UL,",
                f"\t\t.user_physical_address_mask = "
                f"0x{geometry['physical_address_mask']:016x}UL,",
                f"\t\t.user_physical_page_mask = "
                f"0x{geometry['physical_page_mask']:016x}UL,",
                f"\t\t.user_tlbi_all_asid = "
                f"{geometry['tlbi_all_asid']},",
            ])
            if "task_user_state" not in certificate:
                lines.extend([
                    f"\t\t.pt_regs_size = {regs['size']},",
                    f"\t\t.pt_regs_pc = {regs['members']['pc']},",
                    f"\t\t.pt_regs_pstate = {regs['members']['pstate']},",
                ])
        if "file_dentry" in certificate:
            lines.append(
                f"\t\t.file_dentry = {certificate['file_dentry']},"
            )
        lines.append("\t},")
    lines.extend(["};", ""])
    return "\n".join(lines)


def write_or_check(outputs, check):
    for path, rendered in outputs:
        if check:
            current = path.read_text(encoding="utf-8")
            if current != rendered:
                raise ValueError(
                    f"{path} is stale; rerun generate-compat-table.py"
                )
        else:
            path.write_text(rendered, encoding="utf-8")


def profile_contract(profile_evidence, legacy_id):
    """Return the machine-readable, evidence-backed contract for one profile."""
    for profile, manifest, release in profile_evidence:
        if legacy_id in profile.get("aliases", []):
            legacy_id = profile["legacy_id"]
            break
    for profile, manifest, release in profile_evidence:
        if profile["legacy_id"] != legacy_id:
            continue
        layout = compile_layout_contract(manifest)
        return {
            "kcfi_mode": profile["kcfi_mode"],
            "legacy_id": legacy_id,
            "module_exit_offset": layout["module_exit"],
            "module_init_offset": layout["module_init"],
            "module_size": layout["module_size"],
            "shadow_call_stack_mode": profile["shadow_call_stack_mode"],
            "symbol": profile["symbol"],
            "vermagic": release["vermagic"],
        }
    raise ValueError(f"unsupported profile ID {legacy_id}")


def main():
    parser = argparse.ArgumentParser(
        description="generate Android GKI profile policy and compatibility data"
    )
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument(
        "--manifest-root", type=Path, default=DEFAULT_MANIFEST_ROOT
    )
    parser.add_argument(
        "--release-lock", type=Path, default=DEFAULT_RELEASE_LOCK
    )
    parser.add_argument(
        "--layout-certificates",
        type=Path,
        default=DEFAULT_LAYOUT_CERTIFICATES,
    )
    parser.add_argument(
        "--profile-ids-header", type=Path, default=DEFAULT_PROFILE_IDS_HEADER
    )
    parser.add_argument(
        "--profile-header", type=Path, default=DEFAULT_PROFILE_HEADER
    )
    parser.add_argument(
        "--profile-table", type=Path, default=DEFAULT_PROFILE_TABLE
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_COMPAT_TABLE)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if any checked-in generated file is stale",
    )
    parser.add_argument(
        "--query-profile",
        type=int,
        metavar="ID",
        help="print one validated profile contract as JSON instead of generating files",
    )
    args = parser.parse_args()

    try:
        profiles = load_catalog(args.catalog)
        profile_evidence = validate_evidence(
            profiles, args.manifest_root, args.release_lock
        )
        layout_certificates = load_layout_certificates(
            args.layout_certificates, profile_evidence
        )
        if args.query_profile is not None:
            if args.check:
                raise ValueError("--query-profile and --check are mutually exclusive")
            print(
                json.dumps(
                    profile_contract(profile_evidence, args.query_profile),
                    sort_keys=True,
                )
            )
            return 0
        outputs = [
            (args.profile_ids_header, render_profile_ids_header(profiles)),
            (args.profile_header, render_profile_header(profile_evidence)),
            (args.profile_table, render_profile_table(profile_evidence)),
            (
                args.output,
                render_compat_table(profile_evidence, layout_certificates),
            ),
        ]
        write_or_check(outputs, args.check)
    except (json.JSONDecodeError, OSError, ValueError) as error:
        print(f"generate-compat-table: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
DEFAULT_PROFILE_IDS_HEADER = RUNTIME_ROOT / "include/nvk_profile_ids.h"
DEFAULT_PROFILE_HEADER = RUNTIME_ROOT / "include/nvk_profile_config.h"
DEFAULT_PROFILE_TABLE = RUNTIME_ROOT / "src/nvk_profile_table.inc"
DEFAULT_COMPAT_TABLE = RUNTIME_ROOT / "src/nvk_compat_table.inc"

KCFI_MODES = {
    "disabled": ("NEVERC_KRT_KCFI_MODE_DISABLED", 0),
    "classic": ("NEVERC_KRT_KCFI_MODE_CLASSIC", 1),
    "normalized": ("NEVERC_KRT_KCFI_MODE_NORMALIZED", 2),
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
    "symbol",
})
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
SYMBOL_NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")
UINT32_MAX = (1 << 32) - 1

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
    ("task_tasks", "task_struct", "tasks"),
    ("task_usage", "task_struct", "usage"),
    ("task_mm", "task_struct", "mm"),
    ("task_pid", "task_struct", "pid"),
    ("task_thread_pid", "task_struct", "thread_pid"),
    ("task_group_leader", "task_struct", "group_leader"),
    ("task_real_cred", "task_struct", "real_cred"),
    ("task_cred", "task_struct", "cred"),
    ("task_comm", "task_struct", "comm"),
    ("task_nsproxy", "task_struct", "nsproxy"),
    ("task_seccomp", "task_struct", "seccomp"),
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
    ("vma_start", "vm_area_struct", "vm_start"),
    ("vma_end", "vm_area_struct", "vm_end"),
    ("vma_mm", "vm_area_struct", "vm_mm"),
    ("vma_page_prot", "vm_area_struct", "vm_page_prot"),
    ("vma_flags", "vm_area_struct", "vm_flags"),
    ("vma_pgoff", "vm_area_struct", "vm_pgoff"),
    ("vma_file", "vm_area_struct", "vm_file"),
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
)


def member(layouts, structure, field):
    try:
        return layouts[structure]["members"][field]
    except KeyError as error:
        raise ValueError(f"manifest lacks {structure}.{field}") from error


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
    return member(layouts, structure, field)


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
        if not isinstance(original, dict) or set(original) != PROFILE_KEYS:
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
        capabilities = profile.get("capabilities")
        if not isinstance(capabilities, dict) or set(capabilities) != CAPABILITY_KEYS:
            raise ValueError(f"{context}: capability keys do not match schema")
        for key, values in CAPABILITY_ABIS.items():
            if capabilities[key] not in values:
                raise ValueError(f"{context}: invalid {key}")
        if not isinstance(capabilities["ftrace_registration_api"], bool):
            raise ValueError(f"{context}: ftrace_registration_api must be boolean")
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
    if release_ids != catalog_ids:
        raise ValueError("catalog and release-lock profile sets differ")

    result = []
    for profile in profiles:
        legacy_id = profile["legacy_id"]
        manifest_path = manifest_paths[legacy_id]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        release = dict(release_profiles[str(legacy_id)])
        if manifest.get("profile") != legacy_id:
            raise ValueError(f"{manifest_path}: profile mismatch")
        if manifest.get("kernel_name") != profile["kernel_name"]:
            raise ValueError(f"{manifest_path}: kernel_name mismatch")
        if manifest.get("config", {}).get("PAGE_SHIFT") != profile["page_shift"]:
            raise ValueError(f"{manifest_path}: page_shift mismatch")
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
    lines.extend([
        "",
        "#endif /* NEVERC_KRT_PROFILE_IDS_H */",
        "",
    ])
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
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0 */",
        "/* Generated by tools/generate-compat-table.py.  Do not edit. */",
        f"#define NEVERC_KRT_PROFILE_COUNT {len(profile_evidence)}UL",
        "",
        "static const struct neverc_krt_profile",
        "_neverc_krt_profiles[NEVERC_KRT_PROFILE_COUNT] = {",
    ]
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


def render_compat_table(profile_evidence):
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0 */",
        "/*",
        " * Generated by tools/generate-compat-table.py from the checked",
        " * profile catalog and arm64/gki-manifests.  Do not edit by hand.",
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
        file_dentry = (
            member(layouts, "file", "f_path")
            + member(layouts, "path", "dentry")
        )
        lines.extend([
            "\t{",
            f"\t\t.profile_id = {profile['legacy_id']},",
            f"\t\t.module_size = {layouts['module']['size']},",
            f"\t\t.file_dentry_off = {file_dentry},",
            f"\t\t.off_init = {member(layouts, 'module', 'init')},",
            f"\t\t.off_exit = {member(layouts, 'module', 'exit')},",
            "\t\t.layout = {",
        ])
        for output_name, structure, field in LAYOUT_FIELDS:
            lines.append(
                f"\t\t\t.{output_name} = "
                f"{layout_value(layouts, structure, field)},"
            )
        lines.extend(["\t\t},", "\t},"])

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
        if profile["legacy_id"] != legacy_id:
            continue
        layout = compile_layout_contract(manifest)
        return {
            "kcfi_mode": profile["kcfi_mode"],
            "legacy_id": legacy_id,
            "module_exit_offset": layout["module_exit"],
            "module_init_offset": layout["module_init"],
            "module_size": layout["module_size"],
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
            (args.output, render_compat_table(profile_evidence)),
        ]
        write_or_check(outputs, args.check)
    except (json.JSONDecodeError, OSError, ValueError) as error:
        print(f"generate-compat-table: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

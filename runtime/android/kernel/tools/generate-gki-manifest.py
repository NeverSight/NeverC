#!/usr/bin/env python3
"""Generate deterministic GKI layout, config, and SDK-export evidence."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile


TOOLS_ROOT = Path(__file__).resolve().parent
STRUCTURES = (
    "attribute",
    "attribute_group",
    "callback_head",
    "completion",
    "cpumask",
    "cred",
    "delayed_work",
    "dentry",
    "dir_context",
    "dev_pm_ops",
    "file",
    "file_operations",
    "filename",
    "firmware",
    "hrtimer",
    "idr",
    "kstat",
    "kobject",
    "kprobe",
    "kref",
    "miscdevice",
    "mm_struct",
    "module",
    "module_kobject",
    "mutex",
    "netlink_kernel_cfg",
    "nf_hook_ops",
    "nf_hook_state",
    "nlmsghdr",
    "notifier_block",
    "nsproxy",
    "optimistic_spin_queue",
    "path",
    "proc_ops",
    "pt_regs",
    "qstr",
    "rb_node",
    "rb_root",
    "regmap_config",
    "resource",
    "rw_semaphore",
    "scatterlist",
    "seccomp",
    "semaphore",
    "seq_operations",
    "sg_table",
    "signal_struct",
    "sk_buff",
    "sock_common",
    "task_struct",
    "tasklet_struct",
    "thread_info",
    "timer_list",
    "timespec64",
    "vm_area_struct",
    "vmap_area",
    "wait_queue_entry",
    "wait_queue_head",
    "work_struct",
)
OPTIONAL_STRUCTURES = ("inode",)
MODULE_MEMORY_STRUCTURES = ("module_layout", "module_memory")
MEMBER_SIZE_MEMBERS = {
    "cred": frozenset({
        "egid",
        "euid",
        "fsgid",
        "fsuid",
        "gid",
        "sgid",
        "suid",
        "uid",
    }),
    "dentry": frozenset({"d_inode"}),
    "dir_context": frozenset({"actor", "count", "dt_flags_mask", "pos"}),
    "filename": frozenset({"name"}),
    "inode": frozenset({
        "i_atime",
        "i_atime_nsec",
        "i_atime_sec",
        "i_mtime",
        "i_mtime_nsec",
        "i_mtime_sec",
    }),
    "path": frozenset({"dentry"}),
    "mm_struct": frozenset({
        "mm_count",
        "mmap_lock",
        "page_table_lock",
        "pgd",
    }),
    "pt_regs": frozenset({"pc", "pstate", "regs", "sp"}),
    "signal_struct": frozenset({"thread_head"}),
    "task_struct": frozenset({
        "comm",
        "flags",
        "group_leader",
        "mm",
        "parent",
        "pid",
        "real_cred",
        "real_parent",
        "signal",
        "stack",
        "stack_refcount",
        "thread_node",
        "thread_pid",
        "tasks",
        "usage",
    }),
    "timespec64": frozenset({"tv_nsec", "tv_sec"}),
    "vm_area_struct": frozenset({"vm_end", "vm_start"}),
}
FILTERED_STRUCTURE_MEMBERS = {
    # struct filename is opaque to SDK callers; retain only the accessor field.
    "filename": MEMBER_SIZE_MEMBERS["filename"],
    # struct inode is intentionally a narrow private-field certificate input;
    # do not expand the checked manifest with unrelated VFS internals.
    "inode": MEMBER_SIZE_MEMBERS["inode"],
    # User page-table operations consume mm only through a narrow, generated
    # runtime contract.  Do not retain unrelated private mm fields.
    "mm_struct": MEMBER_SIZE_MEMBERS["mm_struct"],
    # Thread enumeration is profile-backed, but only this list anchor is part
    # of the private signal_struct contract.
    "signal_struct": MEMBER_SIZE_MEMBERS["signal_struct"],
}
CONFIG_KEYS = (
    "CONFIG_ARM64_4K_PAGES",
    "CONFIG_ARM64_16K_PAGES",
    "CONFIG_ARM64_64K_PAGES",
    "CONFIG_ARM64_PA_BITS",
    "CONFIG_ARM64_VA_BITS",
    "CONFIG_CFI_CLANG",
    "CONFIG_COMPAT",
    "CONFIG_DEBUG_INFO_BTF_MODULES",
    "CONFIG_DEBUG_LOCK_ALLOC",
    "CONFIG_DEBUG_SPINLOCK",
    "CONFIG_KASAN_GENERIC",
    "CONFIG_KASAN_SW_TAGS",
    "CONFIG_MMU",
    "CONFIG_MODULE_UNLOAD",
    "CONFIG_NR_CPUS",
    "CONFIG_PGTABLE_LEVELS",
    "CONFIG_PREEMPT_RT",
    "CONFIG_SECCOMP",
    "CONFIG_VMAP_STACK",
)
CONFIG_INPUT_KEYS = (*CONFIG_KEYS, "CONFIG_CFI")
SHF_COMPRESSED = 0x800
ELF_HASH_CHUNK_SIZE = 1024 * 1024


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, TOOLS_ROOT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def update_digest_from_elf_section(
    digest,
    section,
    chunk_size=ELF_HASH_CHUNK_SIZE,
):
    """Hash section contents without materializing an uncompressed section."""
    if chunk_size <= 0:
        raise ValueError("ELF hash chunk size must be positive")
    if int(section["sh_flags"]) & SHF_COMPRESSED:
        # pyelftools owns the compression format handling.  Compressed debug
        # sections are uncommon in the pinned GKI assets; retain the existing
        # decompressed-byte digest semantics for them.
        digest.update(section.data())
        return

    stream = section.stream
    original_offset = stream.tell()
    remaining = int(section["sh_size"])
    try:
        stream.seek(int(section["sh_offset"]))
        while remaining:
            chunk = stream.read(min(chunk_size, remaining))
            if not chunk:
                raise ValueError("ELF section is truncated while hashing")
            digest.update(chunk)
            remaining -= len(chunk)
    finally:
        stream.seek(original_offset)


def elf_section_sha256(section, chunk_size=ELF_HASH_CHUNK_SIZE):
    digest = hashlib.sha256()
    update_digest_from_elf_section(digest, section, chunk_size)
    return digest.hexdigest()


def parse_config_value(value):
    if value == "y":
        return True
    if value == "n":
        return False
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    try:
        return int(value, 0)
    except ValueError:
        return value


def read_config(path):
    values = {key: False for key in CONFIG_INPUT_KEYS}
    with path.open(encoding="utf-8") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if not line.startswith("CONFIG_") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key in values:
                values[key] = parse_config_value(value)

    # Android 17 / Linux 6.18 uses the upstream CONFIG_CFI spelling. Keep the
    # manifest's stable CONFIG_CFI_CLANG field while accepting either source
    # key, and never silently downgrade modern CFI to false.
    modern_cfi = values.pop("CONFIG_CFI")
    values["CONFIG_CFI_CLANG"] = bool(
        values["CONFIG_CFI_CLANG"] or modern_cfi
    )
    page_sizes = {
        "CONFIG_ARM64_4K_PAGES": 12,
        "CONFIG_ARM64_16K_PAGES": 14,
        "CONFIG_ARM64_64K_PAGES": 16,
    }
    selected = [
        shift for key, shift in page_sizes.items()
        if values.get(key) is True
    ]
    if len(selected) != 1:
        raise ValueError("GKI config must select exactly one ARM64 page size")
    values["PAGE_SHIFT"] = selected[0]
    return values


def arm64_thread_size(vmlinux, config):
    """Measure THREAD_SIZE from vmlinux, with config fallback for old kernels."""
    with vmlinux.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        values = {}
        if symbols is not None:
            for name in ("__start_init_stack", "__end_init_stack"):
                matches = symbols.get_symbol_by_name(name)
                if matches:
                    if len(matches) != 1:
                        raise ValueError(
                            f"vmlinux has ambiguous {name} stack evidence"
                        )
                    values[name] = int(matches[0]["st_value"])

    if values:
        if len(values) != 2:
            raise ValueError("vmlinux has incomplete thread-stack boundaries")
        size = values["__end_init_stack"] - values["__start_init_stack"]
    else:
        page_shift = config["PAGE_SHIFT"]
        minimum_shift = 14 + int(
            config.get("CONFIG_KASAN_GENERIC") is True
            or config.get("CONFIG_KASAN_SW_TAGS") is True
        )
        if (
            config.get("CONFIG_VMAP_STACK") is True
            and minimum_shift < page_shift
        ):
            size = 1 << page_shift
        else:
            size = 1 << minimum_shift

    if (
        size < (1 << 14)
        or size > (1 << 20)
        or size & (size - 1)
    ):
        raise ValueError("vmlinux has invalid ARM64 THREAD_SIZE evidence")
    return size


def bind_arm64_runtime_config(config, vmlinux):
    """Bind config-derived facts to runtime geometry measured from vmlinux."""
    bound = dict(config)
    bound["THREAD_SIZE"] = arm64_thread_size(vmlinux, bound)
    return bound


def gnu_build_id(elf):
    """Return the GNU build ID from any ELF note section or segment."""

    def build_id_from_notes(notes):
        for note in notes:
            name = note["n_name"]
            if isinstance(name, bytes):
                name = name.rstrip(b"\0").decode("ascii", errors="replace")
            else:
                name = str(name).rstrip("\0")
            if name != "GNU":
                continue
            if note["n_type"] not in ("NT_GNU_BUILD_ID", 3):
                continue

            value = note["n_desc"]
            if isinstance(value, bytes):
                return value.hex()
            return str(value)
        return None

    for section in elf.iter_sections():
        if section["sh_type"] != "SHT_NOTE":
            continue
        build_id = build_id_from_notes(section.iter_notes())
        if build_id is not None:
            return build_id

    # Some stripped ELF files retain PT_NOTE while omitting note sections.
    for segment in elf.iter_segments():
        if segment["p_type"] != "PT_NOTE":
            continue
        build_id = build_id_from_notes(segment.iter_notes())
        if build_id is not None:
            return build_id
    return None


def elf_evidence(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        btf = elf.get_section_by_name(".BTF")
        if btf is not None:
            layout_format = "BTF"
            layout_sha256 = elf_section_sha256(btf)
        elif elf.has_dwarf_info():
            layout_format = "DWARF"
            digest = hashlib.sha256()
            for section_name in (
                ".debug_info",
                ".debug_abbrev",
                ".debug_str",
            ):
                section = elf.get_section_by_name(section_name)
                if section is None:
                    continue
                digest.update(section_name.encode("ascii"))
                digest.update(b"\0")
                update_digest_from_elf_section(digest, section)
            layout_sha256 = digest.hexdigest()
        else:
            raise ValueError(f"{path}: ELF has no BTF or DWARF evidence")

        build_id = gnu_build_id(elf)
    return {
        "layout_format": layout_format,
        "layout_sha256": layout_sha256,
        "vmlinux_build_id": build_id,
    }


def module_memory_layout(layouts):
    """Project old module_layout and split module_memory to one read contract."""
    module = layouts.get("module", {})
    members = module.get("members", {})
    member_sizes = module.get("member_sizes", {})
    if "mem" in members:
        member_name = "mem"
        entry_name = "module_memory"
    elif "core_layout" in members:
        member_name = "core_layout"
        entry_name = "module_layout"
    else:
        raise ValueError("struct module lacks core_layout or mem allocation evidence")

    entry = layouts.get(entry_name)
    if not isinstance(entry, dict):
        raise ValueError(f"layout evidence is missing required {entry_name}")
    entry_size = entry.get("size")
    member_size = member_sizes.get(member_name)
    entry_members = entry.get("members", {})
    entry_member_sizes = entry.get("member_sizes", {})
    if (
        not isinstance(entry_size, int)
        or entry_size <= 0
        or not isinstance(member_size, int)
        or member_size <= 0
        or member_size % entry_size
        or entry_member_sizes.get("base") != 8
        or entry_member_sizes.get("size") != 4
    ):
        raise ValueError("module allocation layout lacks width/stride evidence")
    count = member_size // entry_size
    if member_name == "core_layout" and count != 1:
        raise ValueError("legacy module core_layout must contain one allocation")
    if member_name == "mem" and count != 7:
        raise ValueError("split module mem must contain seven allocations")
    result = {
        "base_offset": entry_members.get("base"),
        "count": count,
        "memory_offset": members[member_name],
        "size_offset": entry_members.get("size"),
        "stride": entry_size,
    }
    if any(not isinstance(value, int) or value < 0 for value in result.values()):
        raise ValueError("module allocation layout contains an invalid offset")
    if (
        result["base_offset"] + 8 > entry_size
        or result["size_offset"] + 4 > entry_size
    ):
        raise ValueError("module allocation fields exceed their entry")
    return result


def sdk_export_evidence(compiler, profile, symvers):
    export_tool = load_tool("nvk_check_sdk_exports", "check-sdk-exports.py")
    declarations = export_tool.sdk_declarations(compiler, profile)
    exports = export_tool.exported_symbols(symvers)
    missing = sorted(declarations - set(exports))
    namespaced = sorted(
        name for name in declarations.intersection(exports) if exports[name]
    )
    if missing:
        raise ValueError(
            "SDK declarations missing from GKI exports: " + ", ".join(missing)
        )
    if namespaced:
        raise ValueError(
            "SDK declarations require unsupported namespaces: "
            + ", ".join(namespaced)
        )
    return sorted(declarations)


def main():
    parser = argparse.ArgumentParser(
        description="generate a checked GKI config/layout/export manifest"
    )
    parser.add_argument("--profile", required=True, type=int)
    parser.add_argument("--kernel-name", required=True)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--vmlinux", required=True, type=Path)
    parser.add_argument("--symvers", type=Path)
    parser.add_argument(
        "--base-manifest",
        type=Path,
        help="retain config/export evidence when only a vmlinux is available",
    )
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    try:
        base = None
        if args.base_manifest is not None:
            base = json.loads(args.base_manifest.read_text(encoding="utf-8"))
            if base.get("profile") != args.profile:
                raise ValueError("base manifest profile does not match --profile")
            if base.get("kernel_name") != args.kernel_name:
                raise ValueError(
                    "base manifest kernel name does not match --kernel-name"
                )
        if args.config is None and base is None:
            raise ValueError("--config is required without --base-manifest")
        if args.symvers is None and base is None:
            raise ValueError("--symvers is required without --base-manifest")

        layout_tool = load_tool(
            "nvk_extract_btf_layouts", "extract-btf-layouts.py"
        )
        print(
            f"generate-gki-manifest: extracting GKI {args.profile} layouts",
            flush=True,
        )
        requested_structures = STRUCTURES
        if base is None:
            requested_structures += OPTIONAL_STRUCTURES
        else:
            requested_structures += tuple(
                structure for structure in OPTIONAL_STRUCTURES
                if structure in base.get("layouts", {})
            )
        layouts = layout_tool.extract_layouts(
            args.vmlinux, requested_structures + MODULE_MEMORY_STRUCTURES
        )
        projected_module_memory = module_memory_layout(layouts)
        for auxiliary_structure in MODULE_MEMORY_STRUCTURES:
            layouts.pop(auxiliary_structure, None)
        for structure, layout in layouts.items():
            filtered_members = FILTERED_STRUCTURE_MEMBERS.get(structure)
            if filtered_members is not None:
                layout["members"] = {
                    member: offset
                    for member, offset in layout.get("members", {}).items()
                    if member in filtered_members
                }
                layout.pop("bitfields", None)
            retained_members = MEMBER_SIZE_MEMBERS.get(structure)
            if retained_members is None:
                layout.pop("member_sizes", None)
            elif "member_sizes" in layout:
                layout["member_sizes"] = {
                    member: width
                    for member, width in layout["member_sizes"].items()
                    if member in retained_members
                }
        print(
            f"generate-gki-manifest: extracted {len(layouts)} layouts",
            flush=True,
        )
        missing = sorted(set(requested_structures) - set(layouts))
        if missing:
            raise ValueError(
                "layout evidence is missing required structures: "
                + ", ".join(missing)
            )

        evidence = elf_evidence(args.vmlinux)
        if args.config is not None:
            config = read_config(args.config)
            evidence["config_sha256"] = sha256_file(args.config)
        else:
            config = base["config"]
            evidence["config_sha256"] = base["evidence"]["config_sha256"]
        config = bind_arm64_runtime_config(config, args.vmlinux)
        if args.symvers is not None:
            evidence["symvers_sha256"] = sha256_file(args.symvers)
            sdk_exports = sdk_export_evidence(
                args.compiler, args.profile, args.symvers
            )
        else:
            evidence["symvers_sha256"] = base["evidence"]["symvers_sha256"]
            sdk_exports = base["sdk_exports"]
        manifest = {
            "config": config,
            "evidence": evidence,
            "kernel_name": args.kernel_name,
            "layouts": layouts,
            "module_memory_layout": projected_module_memory,
            "profile": args.profile,
            "schema": 1,
            "sdk_exports": sdk_exports,
        }
        args.output.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (
        json.JSONDecodeError,
        OSError,
        RuntimeError,
        ValueError,
    ) as error:
        print(f"generate-gki-manifest: {error}", file=sys.stderr)
        return 1

    print(
        f"generate-gki-manifest: GKI {args.profile}, "
        f"{len(layouts)} layouts, {len(sdk_exports)} exports -> {args.output}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

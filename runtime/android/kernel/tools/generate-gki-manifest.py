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
    "dev_pm_ops",
    "file",
    "file_operations",
    "firmware",
    "hrtimer",
    "idr",
    "kstat",
    "kobject",
    "kprobe",
    "kref",
    "miscdevice",
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
    "CONFIG_MMU",
    "CONFIG_MODULE_UNLOAD",
    "CONFIG_NR_CPUS",
    "CONFIG_PGTABLE_LEVELS",
    "CONFIG_PREEMPT_RT",
    "CONFIG_SECCOMP",
)


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
    values = {key: False for key in CONFIG_KEYS}
    with path.open(encoding="utf-8") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if not line.startswith("CONFIG_") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key in values:
                values[key] = parse_config_value(value)

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
            layout_sha256 = hashlib.sha256(btf.data()).hexdigest()
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
                digest.update(section.data())
            layout_sha256 = digest.hexdigest()
        else:
            raise ValueError(f"{path}: ELF has no BTF or DWARF evidence")

        build_id = gnu_build_id(elf)
    return {
        "layout_format": layout_format,
        "layout_sha256": layout_sha256,
        "vmlinux_build_id": build_id,
    }


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
        layouts = layout_tool.extract_layouts(args.vmlinux, STRUCTURES)
        missing = sorted(set(STRUCTURES) - set(layouts))
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

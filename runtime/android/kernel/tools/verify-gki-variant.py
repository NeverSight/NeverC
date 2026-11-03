#!/usr/bin/env python3
"""Verify a measured runtime variant from complete, freshly built artifacts."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile

TOOLS = Path(__file__).resolve().parent
KERNEL = TOOLS.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_current_register(module_path, expected):
    # AArch64 MRS encodings with the destination-register bits masked out.
    registers = {0xD5384100: "sp_el0", 0xD53BD060: "tpidrro_el0"}
    with module_path.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        matches = symbols.get_symbol_by_name("probe_current") if symbols else None
        if not matches or len(matches) != 1:
            raise ValueError("native module needs the retained current-task probe")
        symbol = matches[0]
        section = elf.get_section(symbol["st_shndx"])
        offset = int(symbol["st_value"]) - int(section["sh_addr"])
        code = section.data()[offset:offset + int(symbol["st_size"])]
        observed = {registers[word] for index in range(0, len(code) - 3, 4)
                    if (word := int.from_bytes(code[index:index + 4], "little") & ~31)
                    in registers}
        if observed != {expected}:
            raise ValueError(f"native current-task register {observed} differs from {expected}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--neverc", type=Path)
    args = parser.parse_args()
    compat, verifier = load("generate-compat-table"), load("verify-gki-release")
    families = compat.validate_evidence(compat.load_catalog(compat.DEFAULT_CATALOG),
                                       compat.DEFAULT_MANIFEST_ROOT, compat.DEFAULT_RELEASE_LOCK)
    variants = compat.load_variant_evidence(compat.DEFAULT_VARIANTS, families)
    matches = [entry for entry in variants if entry[0]["variant_key"] == args.variant]
    if len(matches) != 1:
        raise ValueError(f"unknown variant {args.variant}")
    profile, checked, release = matches[0]
    root = args.artifacts.resolve()
    checked_path = KERNEL / "arm64/gki-variant-manifests" / f"{args.variant}.json"
    with tempfile.TemporaryDirectory(prefix="neverc-gki-variant-") as temporary:
        fresh = Path(temporary) / "manifest.json"
        subprocess.run([
            sys.executable, str(TOOLS / "generate-gki-manifest.py"),
            "--runtime-variant", "--profile", str(profile["legacy_id"]),
            "--kernel-name", profile["kernel_name"], "--compiler", args.compiler,
            "--vmlinux", str(root / "dist/vmlinux"), "--config", str(root / "config"),
            "--symvers", str(root / "dist/Module.symvers"), "--output", str(fresh),
        ], check=True)
        measured = json.loads(fresh.read_text())
        for field in ("config", "layouts", "module_memory_layout", "sdk_exports"):
            if measured[field] != checked[field]:
                raise ValueError(f"fresh {field} differs from checked variant")
    for image in ("Image", "vmlinux"):
        verifier.verify_linux_banner_path(root / "dist" / image, release["vermagic"])
    abi = verifier.inspect_offset_module(args.module, release["vermagic"],
                                         compat.EXPECTED_KCFI_TYPEIDS[profile["kcfi_mode"]])
    verify_current_register(args.module, profile.get("current_task_register", "sp_el0"))
    contract = compat.compile_loader_abi_contract(checked)
    for field, expected in (("size", "module_size"), ("init", "module_init"),
                            ("exit", "module_exit")):
        if abi[field] != contract[expected]:
            raise ValueError(f"native module {field} differs from measured loader ABI")
    if compat.variant_procmap_layout(checked) != profile["capabilities"]["procmap_ioctl"]:
        raise ValueError("procmap capability differs from measured BTF context")
    if args.neverc:
        load("verify-sdk-layouts").verify_profile(args.neverc, checked_path)
    print(f"GKI_VARIANT_ABI_PASS {args.variant}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"verify-gki-variant: {error}", file=sys.stderr)
        sys.exit(1)

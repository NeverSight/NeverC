#!/usr/bin/env python3
"""Build and inspect six zero-import GKI loader/entry-ABI smoke modules."""

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys


TOOLS = Path(__file__).resolve().parent
REPO_ROOT = TOOLS.parents[3]


def load_verifier():
    spec = importlib.util.spec_from_file_location(
        "verify_gki_release", TOOLS / "verify-gki-release.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load verify-gki-release.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def resolve_tool(value, label):
    result = shutil.which(value)
    if result is None:
        raise RuntimeError(f"{label} is not on PATH: {value}")
    return result


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="build checked zero-import NeverC GKI smoke modules"
    )
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--lock",
        type=Path,
        default=REPO_ROOT / "runtime/android/kernel/arm64/gki-release.json",
    )
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--readelf")
    parser.add_argument(
        "--profiles",
        default="all",
        help="all or a comma-separated subset of 510,515,601,606,612,618",
    )
    return parser.parse_args(argv)


def patch_entry_prefixes(module, records, expected, byteorder):
    module = Path(module)
    zero = bytes(4)
    if expected is None:
        for name, record in records.items():
            prefix = record["prefix"]
            if prefix not in (None, zero):
                raise RuntimeError(
                    f"non-KCFI profile has a nonzero {name} entry prefix"
                )
        return

    writes = []
    for name in ("init_module", "cleanup_module"):
        record = records[name]
        if record["file_offset"] is None or record["section_offset"] < 4:
            raise RuntimeError(f"{name} has no prefix space for its KCFI type ID")
        if record["prefix"] != zero:
            raise RuntimeError(f"{name} KCFI prefix space is not zero-filled")
        value = int(expected[name], 16)
        writes.append(
            (record["file_offset"], value.to_bytes(4, byteorder=byteorder), name)
        )

    with module.open("r+b") as stream:
        for offset, payload, _name in writes:
            stream.seek(offset)
            stream.write(payload)


def apply_locked_kcfi_typeids(module, expected, verify):
    records, byteorder = verify.entry_prefix_records(module)
    patch_entry_prefixes(module, records, expected, byteorder)
    checked, checked_byteorder = verify.entry_prefix_records(module)
    if checked_byteorder != byteorder:
        raise RuntimeError("module byte order changed while applying KCFI type IDs")

    if expected is None:
        for name, record in checked.items():
            if record["prefix"] not in (None, bytes(4)):
                raise RuntimeError(f"unexpected KCFI type ID remained on {name}")
        print("[smoke] KCFI entry ABI: disabled")
        return None

    actual = {}
    for name in sorted(expected):
        prefix = checked[name]["prefix"]
        if not isinstance(prefix, bytes) or len(prefix) != 4:
            raise RuntimeError(f"missing patched KCFI type ID for {name}")
        actual[name] = f"0x{int.from_bytes(prefix, byteorder):08x}"
    if actual != expected:
        raise RuntimeError(
            f"patched KCFI type IDs mismatch: expected {expected!r}, got {actual!r}"
        )
    rendered = " ".join(f"{name}={value}" for name, value in actual.items())
    print(f"[smoke] KCFI entry ABI: {rendered}")
    return actual


def main(argv=None):
    args = parse_args(argv)
    try:
        verify = load_verifier()
        lock = verify.load_lock(args.lock)
        compiler = resolve_tool(args.compiler, "NeverC compiler")
        nm = resolve_tool(args.nm, "nm")
        readelf = verify.find_readelf(args.readelf)
        if args.profiles == "all":
            profiles = list(verify.EXPECTED_PROFILES)
        else:
            profiles = [value.strip() for value in args.profiles.split(",")]
            if not profiles or any(
                value not in verify.EXPECTED_PROFILES for value in profiles
            ):
                raise RuntimeError("--profiles contains an unsupported profile")
            if len(profiles) != len(set(profiles)):
                raise RuntimeError("--profiles contains a duplicate profile")

        args.output_dir.mkdir(parents=True, exist_ok=True)
        source = TOOLS / "gki-qemu-smoke-module.c"
        verifier = REPO_ROOT / "utils/build/verify_gki_offsets.sh"
        header = REPO_ROOT / "runtime/android/kernel/include/nvkmod_version.h"
        index = {"schema": 1, "modules": {}}
        for profile in profiles:
            entry = lock["profiles"][profile]
            output = args.output_dir / f"neverc-gki-smoke-{profile}.ko"
            command = [
                compiler,
                "--target=aarch64-linux-android",
                "-fandroid-kernel-driver-mode",
                f"-DNVK_KERNEL={profile}",
                f'-DNEVERC_KRT_VERMAGIC="{entry["vermagic"]}"',
                "-std=gnu11",
                "-Wall",
                "-Werror",
                "-r",
                "-nostdlib",
                "-o",
                str(output),
                str(source),
            ]
            result = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if result.stdout:
                print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
            if result.returncode != 0:
                raise RuntimeError(
                    f"NeverC failed to build profile {profile} with status {result.returncode}"
                )

            kcfi_typeids = apply_locked_kcfi_typeids(
                output, entry["kcfi_typeids"], verify
            )

            undefined = subprocess.run(
                [nm, "-u", str(output)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            if undefined.returncode != 0:
                raise RuntimeError(
                    f"nm failed for profile {profile}: {undefined.stderr.strip()}"
                )
            if undefined.stdout.strip():
                raise RuntimeError(
                    f"profile {profile} smoke module has undefined imports:\n"
                    + undefined.stdout
                )

            details = verify.inspect_offset_module(output, entry["vermagic"])
            verify.run_offset_verifier(profile, output, verifier, header, readelf)
            digest = verify.sha256_file(output)
            index["modules"][profile] = {
                "file": output.name,
                "kcfi_typeids": kcfi_typeids,
                "sha256": digest,
                "size": output.stat().st_size,
                "vermagic": details["vermagic"],
            }
            print(
                f"[smoke] PASS profile={profile} imports=0 "
                f"sha256={digest} file={output}"
            )

        (args.output_dir / "index.json").write_text(
            json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, RuntimeError) as error:
        print(f"build-gki-smoke-modules: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

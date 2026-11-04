#!/usr/bin/env python3
"""Build the split-TU, first-current-in-hardirq regression for GKI QEMU."""
import argparse
import json
from pathlib import Path
import subprocess

TOOLS = Path(__file__).resolve().parent
KERNEL = TOOLS.parent
MODES = {"auto": [], "full": ["-flto=full"], "none": ["-fno-lto"]}


def build(compiler, output, key, profile, register):
    common = [str(compiler), "--target=aarch64-linux-android",
              "-fandroid-kernel-driver-mode", f"-DNVK_KERNEL={profile}",
              f"-I{KERNEL / 'arm64/include'}", f"-I{KERNEL / 'include'}", "-O2"]
    if register == "tpidrro_el0":
        common.append("-DNEVERC_GKI_EXPECT_TPIDRRO_EL0=1")
    elif register != "sp_el0":
        raise ValueError(f"unsupported native current register: {register}")
    for mode, flags in MODES.items():
        directory = output / key / mode
        directory.mkdir(parents=True, exist_ok=True)
        objects = []
        for unit in ("module", "callback"):
            source = TOOLS / f"gki-current-smoke-{unit}.c"
            obj = directory / f"{unit}.o"
            subprocess.run(common + flags + ["-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        module = directory / "current.ko"
        subprocess.run(common + flags + ["-r", "-nostdlib", *objects,
                                        "-o", str(module)], check=True)
        print(f"GKI_CURRENT_BUILD_PASS {key} {mode}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--profiles", help="comma-separated original family IDs, or all")
    selection.add_argument("--variant", help="measured runtime alternative key")
    args = parser.parse_args()
    if args.variant:
        records = json.loads((KERNEL / "arm64/gki-variants.json").read_text())["variants"]
        selected = [(r["key"], r["profile_id"], r["profile"])
                    for r in records if r["key"] == args.variant]
        if not selected:
            parser.error("unknown variant")
    else:
        records = json.loads((KERNEL / "arm64/gki-profiles.json").read_text())["profiles"]
        keys = {str(r["legacy_id"]) for r in records}
        requested = keys if args.profiles == "all" else set(args.profiles.split(","))
        if not requested or not requested <= keys:
            parser.error("unknown original profile")
        selected = [(str(r["legacy_id"]), r["legacy_id"], r)
                    for r in records if str(r["legacy_id"]) in requested]
    for key, profile, record in selected:
        build(args.compiler.resolve(), args.output_dir.resolve(), key, profile,
              record.get("current_task_register", "sp_el0"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare Hyper-V page intrinsics/source with independent native calls.

KVM forwards calls to a synthetic userspace service. This qualifies register
transport, including rep/fast outputs, not Microsoft's service implementations.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "tests/fixtures/hyperv-context"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build-neverc/bin/neverc")
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--llvm-only", action="store_true")
    parser.add_argument("--new-pm", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    if (args.new_pm or args.llvm_only) and not args.backend_llc:
        parser.error("--new-pm and --llvm-only require --backend-llc")
    if bool(args.source) != bool(args.manifest) or (args.source and args.llvm_only):
        parser.error("source requires manifest and the NeverC frontend")
    if args.source:
        manifest = json.loads(args.manifest.read_text())
        assert manifest["source_sha256"] == digest(args.source)
        assert manifest["native_requests"] == 0
        assert int(str(manifest["request"]["native_hyperv_hypercall_entry"]), 0) == 0x62000000
    args.keep.mkdir(parents=True, exist_ok=True)
    directory = args.keep.resolve()
    paths = {name: shutil.which(name) for name in ("clang", "as", "ld", "objcopy")}
    if not all(paths.values()):
        parser.error("Clang and GNU binutils are required")
    if not args.llvm_only:
        paths["compiler"] = str(args.compiler.resolve())
    if args.backend_llc:
        paths["llc"] = str(args.backend_llc.resolve())
    hashes = {name: digest(path) for name, path in paths.items()}
    commands = []

    def run(command, timeout=180):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=timeout)
        commands.append({"argv": command, "returncode": result.returncode})
        (directory / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
        return result

    run([paths["clang"], "-O2", FIXTURE / "runner.c", "-o", directory / "runner"])
    triple = "x86_64"
    run([paths["as"], "--64", FIXTURE / "original.S", "-o", "original.o"])
    (directory / "guest.c").write_text(
        ((args.source.read_text() + "\n#define NEVERD_HYPERV_CONTINUATION 1\n") if args.source else "") +
        (FIXTURE / "guest.c").read_text())
    (directory / "link.ld").write_text("ENTRY(_start)\nSECTIONS { . = 0x10000; .text : { *(.start) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) } }\n")
    profiles = []
    for opt in ("O0", "O2"):
        flags = ["-" + opt, "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-mno-red-zone", "-fno-pic", "-fno-pie"]
        objects = ["original.o", opt + ".o"]
        if args.llvm_only:
            ir = """declare void @llvm.x86.hyperv.call(ptr,ptr,ptr,i32 immarg)
define void @native_hyperv(ptr %e,ptr %g,ptr %v) {
call void @llvm.x86.hyperv.call(ptr %e,ptr %g,ptr %v,i32 64)
ret void
}
"""
            (directory / (opt + ".ll")).write_text(ir)
            run([paths["clang"], f"--target={triple}-unknown-none-elf", *flags, "-DEXTERNAL_NATIVE", "-c", "guest.c", "-o", "main.o"])
            objects.append("main.o")
        else:
            command = [paths["compiler"], "--target=x86_64-unknown-linux-gnu", "-std=c23", "-fno-lto", "-fno-builtin-mimalloc", *flags]
            run([*command, *(["-S", "-emit-llvm"] if args.backend_llc else ["-c"]),
                 "guest.c", "-o", opt + (".ll" if args.backend_llc else ".o")])
        if args.backend_llc:
            run([paths["llc"], f"-mtriple={triple}", "-filetype=obj", "-verify-machineinstrs", "-" + opt,
                 *(["--enable-new-pm"] if args.new_pm else []), opt + ".ll", "-o", opt + ".o"])
        run([paths["ld"], "-m", "elf_x86_64", "-T", "link.ld", *objects, "-o", opt + ".elf"])
        run([paths["objcopy"], "-O", "binary", opt + ".elf", opt + ".bin"])
        result = run([directory / "runner", opt + ".bin", 64], 30)
        (directory / (opt + ".log")).write_text(result.stdout + result.stderr)
        assert result.stdout == "PASS calls=768\n", result.stdout
        profiles.append({"optimization": opt, "original_source_pairs": 384, "native_hypercalls": 768})
        print(f"{opt}: 384 original/candidate pairs, 768 native Hyper-V page hypercalls", flush=True)
    assert hashes == {name: digest(path) for name, path in paths.items()}
    record = {"passed": True, "hypervisor": "KVM Hyper-V ABI with synthetic userspace service",
              "microsoft_service_semantics_qualified": False, "profiles": profiles, "tools": {name: {"path": paths[name], "sha256": hashes[name]} for name in paths},
              "new_pass_manager": args.new_pm, "llvm_only": args.llvm_only, "code_bits": 64,
              "source_sha256": digest(args.source) if args.source else None,
              "manifest_sha256": digest(args.manifest) if args.manifest else None,
              "files": {p.name: digest(p) for p in directory.iterdir() if p.is_file() and p.name != "qualification.json"},
              "fixture_sha256": {p.name: digest(p) for p in FIXTURE.iterdir() if p.is_file()},
              "scope": "Hyper-V native page register transport with a synthetic KVM userspace service; not Microsoft service semantics"}
    (directory / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()

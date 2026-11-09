#!/usr/bin/env python3
"""Execute native KVM ABI intrinsics or actual NeverD C against original bytes."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "tests/fixtures/kvm-hypercall"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build-neverc/bin/neverc")
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--llvm-only", action="store_true")
    parser.add_argument("--new-pm", action="store_true")
    parser.add_argument("--bits", type=int, choices=(64,), default=64)
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
        assert manifest["request"]["native_kvm_hypercalls"] is True
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
    run([paths["as"], f"--{args.bits}", FIXTURE / "original.S", "-o", "original.o"])
    (directory / "guest.c").write_text(
        ((args.source.read_text() + "\n#define NEVERD_KVM_CONTINUATION 1\n") if args.source else "") +
        (FIXTURE / "guest.c").read_text())
    (directory / "link.ld").write_text("ENTRY(_start)\nSECTIONS { . = 0x10000; .text : { *(.start) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) } }\n")
    profiles = []
    for opt in ("O0", "O2"):
        flags = ["-" + opt, "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-mno-red-zone", "-fno-pic", "-fno-pie"]
        objects = ["original.o", opt + ".o"]
        if args.llvm_only:
            ir = []
            word = f"i{args.bits}"
            for name in ("vmcall", "vmmcall"):
                ir.append(f"declare {word} @llvm.x86.{name}.kvm({','.join([word]*5)})\ndefine {word} @native_{name}({','.join(word+' %'+v for v in 'nabcd')}) {{\n%r=call {word} @llvm.x86.{name}.kvm({','.join(word+' %'+v for v in 'nabcd')})\nret {word} %r\n}}\n")
            (directory / (opt + ".ll")).write_text("".join(ir))
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
        result = run([directory / "runner", opt + ".bin", args.bits], 30)
        (directory / (opt + ".log")).write_text(result.stdout + result.stderr)
        assert result.stdout == "PASS calls=512\n", result.stdout
        profiles.append({"optimization": opt, "original_source_pairs": 256, "native_hypercalls": 512})
        print(f"{opt}: 256 original/candidate pairs, 512 native KVM hypercalls", flush=True)
    assert hashes == {name: digest(path) for name, path in paths.items()}
    record = {"passed": True, "profiles": profiles, "tools": {name: {"path": paths[name], "sha256": hashes[name]} for name in paths},
              "new_pass_manager": args.new_pm, "llvm_only": args.llvm_only, "code_bits": args.bits,
              "source_sha256": digest(args.source) if args.source else None,
              "manifest_sha256": digest(args.manifest) if args.manifest else None,
              "files": {p.name: digest(p) for p in directory.iterdir() if p.is_file() and p.name != "qualification.json"},
              "fixture_sha256": {p.name: digest(p) for p in FIXTURE.iterdir() if p.is_file()},
              "scope": "Native KVM ABI transport with an isolated userspace service; not every KVM service or arbitrary hypervisor semantics"}
    (directory / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()

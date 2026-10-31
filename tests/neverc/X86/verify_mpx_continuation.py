#!/usr/bin/env python3
"""Compare actual NeverD V11 C with independent original MPX bytes in QEMU.

Input is the source/manifest exported by MpxOwnsAllBoundsAndMemoryForms.
QEMU limitations are explicit exclusions, not passing instruction cases.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / "tests/fixtures/mpx"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def oracle(units):
    assembly = [".text\n.code64\n"]
    header = ["typedef void (*Original)(uint64_t,uint64_t,void*,void*,uint64_t,void*);\n"]
    cases = []
    excluded = []
    for i, unit in enumerate(units):
        op, bank, prefix = i % 13, (i // 13) % 4, i // 52
        reason = ("LOCK BNDMOV rejected by QEMU" if op == 4 else
                  "register checks read the wrong QEMU decode operand" if 5 <= op <= 7 else
                  "QEMU truncates addr32 MPX memory addresses" if prefix and op in (2, 3, 9, 10, 11, 12) else
                  "absent-base translation is covered by the portable oracle, outside this native fixture" if op == 11 else None)
        if reason:
            excluded.append({"pc": unit["pc"], "bytes": unit["bytes"], "reason": reason})
            continue
        name = f"original_{i}"
        header.append(f"extern void {name}(uint64_t,uint64_t,void*,void*,uint64_t,void*);\n")
        code = bytes.fromhex(unit["bytes"])
        cases.append(f"{{{name},{op},{bank},{prefix},{unit['pc']},{len(code)}}}")
        assembly.append(f"""
.global {name}
{name}:
 push %rbx
 push %r12
 push %r13
 push %r14
 push %r15
 mov %rdx,%rbx
 mov %rcx,%r12
 mov %rdi,%r13
 mov %rsi,%r14
 mov %r9,%r15
 mov $3,%eax
 xor %edx,%edx
 mov $0xd90,%ecx
 wrmsr
 mov $16,%eax
 xrstor64 (%r15)
 .byte 0x66,0x0f,0x1a,0x03
 .byte 0x66,0x0f,0x1a,0x4b,0x10
 .byte 0x66,0x0f,0x1a,0x53,0x20
 .byte 0x66,0x0f,0x1a,0x5b,0x30
 mov %r8d,%eax
 xor %edx,%edx
 mov $0xd90,%ecx
 wrmsr
 mov %r13,%rax
 mov %r14,%rcx
 .byte {','.join(hex(b) for b in code)}
 mov $3,%eax
 xor %edx,%edx
 mov $0xd90,%ecx
 wrmsr
 .byte 0x66,0x41,0x0f,0x1b,0x04,0x24
 .byte 0x66,0x41,0x0f,0x1b,0x4c,0x24,0x10
 .byte 0x66,0x41,0x0f,0x1b,0x54,0x24,0x20
 .byte 0x66,0x41,0x0f,0x1b,0x5c,0x24,0x30
 mov $24,%eax
 xor %edx,%edx
 xsave64 (%r15)
 pop %r15
 pop %r14
 pop %r13
 pop %r12
 pop %rbx
 ret
""")
    header.append("static const struct {Original fn;unsigned op,bank,prefix;uint64_t pc,size;} cases[]={\n")
    header.append(",\n".join(cases) + "};\n")
    assembly.append('.section .note.GNU-stack,"",@progbits\n')
    return "".join(assembly), "".join(header), excluded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build-neverc/bin/neverc")
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--new-pm", action="store_true")
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    if args.new_pm and not args.backend_llc:
        parser.error("--new-pm requires --backend-llc")
    report = json.loads(args.manifest.read_text())
    assert report["profile"] == "intel-mpx-v11" and report["native_requests"] == 0
    assert report["source_sha256"] == digest(args.source)
    assert len(report["units"]) == 156, "expected the complete exported MPX fixture"
    args.keep.mkdir(parents=True, exist_ok=True)
    directory = args.keep.resolve()
    tools = {key: shutil.which(key) for key in ("clang", "as", "ld", "objcopy", "qemu-system-x86_64")}
    if not all(tools.values()):
        parser.error("Clang, GNU binutils and QEMU are required")
    hashes = {path: digest(path) for path in tools.values()}
    hashes[str(args.compiler.resolve())] = digest(args.compiler)
    if args.backend_llc:
        hashes[str(args.backend_llc.resolve())] = digest(args.backend_llc)
    commands = []

    def run(command, qemu=False):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=directory, text=True, capture_output=True,
                                timeout=60 if qemu else 180)
        commands.append({"argv": command, "returncode": result.returncode})
        (directory / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode != (1 if qemu else 0):
            raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
        return result

    for filename in ("guest.S", "boot.S", "boot.ld", "payload.ld"):
        shutil.copyfile(FIXTURES / filename, directory / filename)
    assembly, header, excluded = oracle(report["units"])
    (directory / "original.S").write_text(assembly)
    (directory / "cases.inc").write_text(header)
    (directory / "guest.c").write_text(args.source.read_text() + '\n#include "cases.inc"\n' +
                                       (FIXTURES / "continuation.c").read_text())
    run([tools["as"], "--64", "guest.S", "-o", "entry.o"])
    run([tools["as"], "--64", "original.S", "-o", "original.o"])
    profiles = []
    for opt in ("O0", "O2"):
        command = [args.compiler.resolve(), "--target=x86_64-unknown-linux-gnu", "-std=c23",
                   "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-fno-stack-protector",
                   "-mno-red-zone", "-fno-strict-aliasing", "-" + opt]
        if args.backend_llc:
            run([*command, "-S", "-emit-llvm", "guest.c", "-o", opt + ".ll"])
            run([args.backend_llc.resolve(), "-mtriple=x86_64", "-filetype=obj", "-verify-machineinstrs",
                 "-" + opt, *(["--enable-new-pm"] if args.new_pm else []), opt + ".ll", "-o", opt + ".o"])
        else:
            run([*command, "-c", "guest.c", "-o", opt + ".o"])
        run([tools["ld"], "-m", "elf_x86_64", "-T", "payload.ld", "entry.o", "original.o", opt + ".o", "-o", opt + ".elf"])
        run([tools["objcopy"], "-O", "binary", opt + ".elf", "payload.bin"])
        run([tools["as"], "--32", "boot.S", "-o", "boot.o"])
        run([tools["ld"], "-m", "elf_i386", "-T", "boot.ld", "boot.o", "-o", "boot.elf"])
        result = run([tools["qemu-system-x86_64"], "-machine", "pc,accel=tcg", "-cpu", "max", "-m", "64M",
                      "-nodefaults", "-display", "none", "-no-reboot", "-monitor", "none", "-serial", "none",
                      "-debugcon", "stdio", "-device", "isa-debug-exit,iobase=0xf4,iosize=4", "-kernel", "boot.elf"], True)
        (directory / (opt + ".log")).write_text(result.stdout + result.stderr)
        pairs = (len(report["units"]) - len(excluded)) * 12 * 2
        assert result.stdout == f"PASS{pairs:08x}\n", result.stdout + result.stderr
        profiles.append({"optimization": opt, "original_comparisons": pairs})
        print(f"{opt}: {pairs} original MPX/source comparisons; {len(excluded)} forms excluded for QEMU limitations", flush=True)
    assert hashes == {path: digest(path) for path in hashes}
    record = {"passed": True, "profiles": profiles, "tools": hashes,
              "source_sha256": digest(args.source), "manifest_sha256": digest(args.manifest),
              "excluded_forms": excluded, "codegen_pass_manager": "new" if args.new_pm else "default",
              "files": {p.name: digest(p) for p in directory.iterdir() if p.is_file() and p.name != "qualification.json"},
              "scope": "QEMU TCG successful long-mode RAM and #BR boundary; no physical MPX or memory fault qualification"}
    (directory / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()

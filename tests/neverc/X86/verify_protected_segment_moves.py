#!/usr/bin/env python3
"""Qualify native LLVM segment MOV interfaces in protected code16/code32 using KVM."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parent
REGISTERS = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
WORDS = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")
OPERATIONS = ("readseg", "readseg.mem", "writeseg", "writeseg.mem")
SEGMENTS = ("es", "cs", "ss", "ds", "fs", "gs")


def sources():
    ir = ["declare i16 @llvm.x86.readseg(i32 immarg)\n",
          "declare void @llvm.x86.readseg.mem(i32 immarg, ptr)\n",
          "declare void @llvm.x86.writeseg(i32 immarg, i16)\n",
          "declare void @llvm.x86.writeseg.mem(i32 immarg, ptr)\n",
          "declare void @llvm.x86.sse2.lfence()\n"]
    for kind, operation in enumerate(OPERATIONS):
        for segment, name in enumerate(SEGMENTS):
            if kind >= 2 and segment == 1:
                continue
            for used in ((0, 1) if kind == 0 else (0,)):
                ir.append(f"define i32 @probe_{kind}_{name}_{used}(i32 %value, ptr %p, ptr %marker) #0 {{\n"
                          "  %selector = trunc i32 %value to i16\n"
                          "  store i8 17, ptr %marker\n")
                args = f"i32 {segment}" + ("" if kind == 0 else ", i16 %selector" if kind == 2 else ", ptr %p")
                ir.append(("  %word = call i16" if kind == 0 else "  call void") +
                          f" @llvm.x86.{operation}({args})\n"
                          "  call void @llvm.x86.sse2.lfence()\n"
                          "  store i8 29, ptr %marker\n")
                if used:
                    ir.append("  %wide = zext i16 %word to i32\n")
                ir.append(f"  ret i32 {'%wide' if used else '42'}\n}}\n")
    # LFENCE ends MOV-SS inhibition without changing the represented bank.
    # The generated scalar functions must still contain no SIMD/FP operations.
    ir.append('attributes #0 = { "target-features"="+sse2,-mmx" }\n')
    return "".join(ir)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--mode", type=int, choices=(16, 32), required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(output))
    tools = {name: shutil.which(name) for name in ("clang", "ld.lld", "objcopy")}
    if not all(tools.values()):
        parser.error("clang, ld.lld and objcopy are required")
    tools.update({name: str((args.llvm_bin / name).resolve())
                  for name in ("llc", "opt", "llvm-objdump")})
    records = []

    def run(command, stem, expected=0, timeout=600):
        command = [str(item) for item in command]
        started = time.monotonic()
        result = subprocess.run(command, text=True, capture_output=True,
                                env=env, timeout=timeout)
        (output / f"{stem}.stdout").write_text(result.stdout)
        (output / f"{stem}.stderr").write_text(result.stderr)
        records.append(dict(command=command, exit_code=result.returncode,
                            seconds=time.monotonic() - started,
                            stdout=f"{stem}.stdout", stderr=f"{stem}.stderr"))
        (output / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{stem}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    (output / "input.ll").write_text(sources())
    (output / "guest.ld").write_text("""ENTRY(probe_0_es_0)
SECTIONS {
  . = 0x10000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
""")
    runner = output / "kvm-runner"
    run([tools["clang"], "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         ROOT / "protected_segment_move_kvm.c", "-o", runner], "host-build")
    profiles = []
    for level in ("O0", "O2"):
        source = output / "input.ll"
        if level == "O2":
            optimized = output / "optimized.ll"
            run([tools["opt"], "-S", "-passes=default<O2>", source,
                 "-o", optimized], level + "-opt")
            source = optimized
        obj, elf, binary = (output / (level + suffix)
                            for suffix in (".o", ".elf", ".bin"))
        triple = "i386-unknown-linux-" + ("code16" if args.mode == 16 else "gnu")
        run([tools["llc"], f"-mtriple={triple}", f"-{level}",
             *(["-enable-new-pm"] if level == "O2" else []),
             "-verify-machineinstrs", "-filetype=obj", source, "-o", obj],
            level + "-compile")
        run([tools["ld.lld"], "-m", "elf_i386", "-T", output / "guest.ld",
             obj, "-o", elf], level + "-link")
        run([tools["objcopy"], "-O", "binary", elf, binary], level + "-image")
        assembly = run([tools["llvm-objdump"], "-d",
                        *(["--mattr=+16bit-mode,-32bit-mode"] if args.mode == 16 else []),
                        elf], level + "-assembly")
        functions = re.findall(
            r"^([0-9a-f]+) <(probe_[0-3]_(?:es|cs|ss|ds|fs|gs)_[01])>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
            assembly, re.M | re.S)
        assert len(functions) == 28, assembly
        manifest = []
        first_load = first_selector = None
        for address, name, body in functions:
            kind, segment, used = re.fullmatch(r"probe_([0-3])_(es|cs|ss|ds|fs|gs)_([01])", name).groups()
            kind = int(kind)
            instructions = re.findall(
                r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            accesses = [(int(pc, 16), re.fullmatch(r"movw\s+(.*), (.*)", text))
                        for pc, text in instructions if re.search(r"%(?:es|cs|ss|ds|fs|gs)(?:,|$)", text)]
            assert len(accesses) == 1 and accesses[0][1], body
            pc, match = accesses[0]
            source, destination = match.groups()
            assert (source if kind < 2 else destination) == "%" + segment, body
            value = destination if kind < 2 else source
            if kind & 1:
                base = re.fullmatch(r"\(%(\w+)\)", value)
                assert base, body
                operand = REGISTERS.index(base[1])
            else:
                assert value.startswith("%"), body
                operand = WORDS.index(value[1:])
            fences = [int(pc, 16) for pc, text in instructions if text == "lfence"]
            assert len(fences) == 1 and fences[0] > pc, body
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            manifest.append(f"{name} {address} {pc:x} {fences[0]:x} {kind} {SEGMENTS.index(segment)} {used} {operand}")
            if kind & 1 and first_load is None:
                moves = [(int(address, 16), int(instructions[i + 1][0], 16))
                         for i, (address, text) in enumerate(instructions[:-1])
                         if int(address, 16) < pc and re.fullmatch(r"movl\s+[^,]+, %" + REGISTERS[operand], text)]
                assert moves, body
                first_load = (*moves[-1], operand)
            if kind == 2 and first_selector is None:
                moves = [(int(address, 16), int(instructions[i + 1][0], 16))
                         for i, (address, text) in enumerate(instructions[:-1])
                         if int(address, 16) < pc and re.fullmatch(
                             r"mov(?:w|l|zwl)\s+[^,]+, %(?:" + WORDS[operand] + "|" + REGISTERS[operand] + ")", text)]
                assert moves, body
                first_selector = (*moves[-1], operand)
        manifest_path = output / f"{level}.manifest"
        manifest_path.write_text("\n".join(manifest) + "\n")
        native = json.loads(run([runner, binary, manifest_path, args.mode], level + "-native"))
        assert native["complete_state_pairs"] == 4824, native
        assert all(native[key] for key in ("returns", "np", "ss", "gp", "pf", "ac")), native
        # Keep the native segment instruction intact, but independently corrupt
        # the declared pointer and selector delivered to it.
        for label, transport, needle in (("pointer", first_load, "== pointer"),
                                          ("selector", first_selector, "== selector")):
            damaged = bytearray(binary.read_bytes())
            start, end, register = transport
            if label == "pointer":
                mutation = (b"\x66" if args.mode == 16 else b"") + bytes((0x31, 0xc0 | register << 3 | register))
            else:
                # A nonzero selector also distinguishes the first null input.
                mutation = (b"" if args.mode == 16 else b"\x66") + bytes((0xb8 + register, 16, 0))
            assert end - start >= len(mutation)
            damaged[start - 0x10000:end - 0x10000] = mutation + b"\x90" * (end - start - len(mutation))
            negative = output / f"{level}-wrong-{label}.bin"
            negative.write_bytes(damaged)
            run([runner, negative, manifest_path, args.mode], level + "-wrong-" + label, expected=1)
            assert needle in (output / f"{level}-wrong-{label}.stderr").read_text()
        profiles.append(dict(optimization=level, mode=args.mode,
                             image_sha256=sha256(binary), manifest_sha256=sha256(manifest_path),
                             negative_controls=["wrong pointer before intact segment MOV rejected",
                                                "wrong selector before intact segment MOV rejected"], **native))
        print(f"PASS code{args.mode} {level}: {native}", flush=True)
    report = dict(schema_version=1, tools={k: sha256(v) for k, v in tools.items()},
                  inputs={str(p.relative_to(ROOT)): sha256(p) for p in
                          [Path(__file__), ROOT / "protected_segment_move_kvm.c",
                           ROOT / "KvmTransferFixture.h", ROOT / "KvmProtectedFaultFixture.h"]},
                  profiles=profiles,
                  comparisons=sum(p["complete_state_pairs"] for p in profiles),
                  boundary="CPL0/CPL3 protected code16/code32 with the 32-bit stack/pointer ABI. Compare each native MOV followed immediately by LFENCE, which ends MOV-SS inhibition without changing represented registers/flags/RAM. Original #GP/#NP/#SS/#PF/#AC handlers expose vectors with OUT. ABI-compatible successes also return through the compiler frame. No NeverC i386 frontend, real/VM86, observation inside MOV-SS inhibition or asynchronous events.")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

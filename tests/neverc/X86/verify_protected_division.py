#!/usr/bin/env python3
"""Qualify LLVM native DIV/IDIV in protected code16/code32 using KVM."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from verify_native_division import sources

ROOT = Path(__file__).resolve().parent
REGISTERS = {
    "b": ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"),
    "w": ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di"),
    "l": ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"),
}


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

    ir, _, _ = sources(32)
    # Only the represented integer/system bank is needed. No vector or x87
    # instructions may be introduced by wrapper/result-store legalization.
    ir = ir.replace(") {\n", ') #0 {\n')
    ir += ('attributes #0 = { "target-features"="-sse,-sse2,-mmx" }\n')
    (output / "input.ll").write_text(ir)
    (output / "guest.ld").write_text("""ENTRY(probe_udivrem8_0)
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
         ROOT / "native_division_kvm.c", "-o", runner], "host-build")
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
            r"^([0-9a-f]+) <(probe_[us]divrem\d+_[0-3])>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
            assembly, re.M | re.S)
        assert len(functions) == 24, assembly
        manifest = []
        first_divide = None
        first_divisor_load = None
        for address, name, body in functions:
            operation = re.fullmatch(r"probe_([us])divrem(\d+)_([0-3])", name)
            sign, bits, results = operation.groups()
            instructions = re.findall(
                r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            divisions = [(int(pc, 16), re.fullmatch(r"(i?div)([bwl])\s+%(\w+)", text))
                         for pc, text in instructions if re.match(r"i?div[bwl]\b", text)]
            assert len(divisions) == 1 and divisions[0][1], body
            pc, match = divisions[0]
            opcode, suffix, register = match.groups()
            assert (opcode == "idiv") == (sign == "s"), body
            assert suffix == {"8": "b", "16": "w", "32": "l"}[bits], body
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            operand = REGISTERS[suffix].index(register)
            manifest.append(f"{name} {address} {pc:x} {bits} {int(sign == 's')} {results} {operand}")
            if first_divide is None:
                first_divide = pc
                # The unused byte-result wrapper loads its divisor from the
                # stack. Keep DIV itself intact in a transport counterexample.
                parent = REGISTERS["l"][operand % 4]
                loads = [(int(address, 16), int(instructions[i + 1][0], 16))
                         for i, (address, text) in enumerate(instructions[:-1])
                         if int(address, 16) < pc and
                         re.fullmatch(r"mov(?:l|zbl)\s+[^,]+, %" + parent, text)]
                assert loads, body
                first_divisor_load = (*loads[-1], operand)
        manifest_path = output / f"{level}.manifest"
        manifest_path.write_text("\n".join(manifest) + "\n")
        native = json.loads(run([runner, binary, manifest_path, args.mode], level + "-native"))
        assert native["complete_state_pairs"] == 24 * 12**3 and native["faults"], native
        # Replace the unused-result divide with same-length NOPs. The boundary
        # instruction authenticates original bytes, and the oracle must reject
        # a compiler that erases its only observable #DE side effect.
        damaged = bytearray(binary.read_bytes())
        offset = first_divide - 0x10000
        damaged[offset:offset + 2] = b"\x90\x90"
        negative = output / f"{level}-negative.bin"
        negative.write_bytes(damaged)
        run([runner, negative, manifest_path, args.mode], level + "-negative", expected=1)
        damaged = bytearray(binary.read_bytes())
        start, end, operand = first_divisor_load
        assert end - start >= 2
        damaged[start - 0x10000:end - 0x10000] = (
            bytes((0xb0 + operand, 0)) + b"\x90" * (end - start - 2))
        negative = output / f"{level}-wrong-divisor.bin"
        negative.write_bytes(damaged)
        run([runner, negative, manifest_path, args.mode],
            level + "-wrong-divisor", expected=1)
        assert "reg_value(boundary, operand, bits) == values[di]" in (
            output / f"{level}-wrong-divisor.stderr").read_text()
        profiles.append(dict(optimization=level, mode=args.mode,
                             image_sha256=sha256(binary), manifest_sha256=sha256(manifest_path),
                             negative_controls=["erased unused-result DIV rejected",
                                                "zero divisor substituted before intact DIV rejected"],
                             **native))
        print(f"PASS code{args.mode} {level}: {native}", flush=True)
    report = dict(schema_version=1, tools={k: sha256(v) for k, v in tools.items()},
                  inputs={str(p.relative_to(ROOT)): sha256(p) for p in
                          [Path(__file__), ROOT / "native_division_kvm.c",
                           ROOT / "KvmTransferFixture.h", ROOT / "verify_native_division.py"]},
                  profiles=profiles,
                  comparisons=sum(p["complete_state_pairs"] for p in profiles),
                  boundary="Protected CPL0 code16/code32, 32-bit stack ABI, GPR/system/event/RAM equality at DIV/IDIV or #DE, return outputs and unused-result side effects. No NeverC i386 frontend, operand-memory faults, other privilege levels or asynchronous events.")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

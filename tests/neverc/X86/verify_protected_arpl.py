#!/usr/bin/env python3
"""Qualify native LLVM ARPL in protected code16/code32 against original bytes."""

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


def sources():
    ir = ["declare { i16, i8 } @llvm.x86.arpl.reg(i16, i16)\n",
          "declare i8 @llvm.x86.arpl.mem(ptr, i16)\n"]
    for kind, variants in (("reg", range(4)), ("alias", (0, 3)), ("mem", (0, 2))):
        for results in variants:
            ir.append(f"define i32 @probe_{kind}_{results}(i32 %a, i32 %b, ptr %p, ptr %marker) #0 {{\n"
                      "  %d = trunc i32 %a to i16\n"
                      "  %s = trunc i32 %b to i16\n"
                      "  store i8 17, ptr %marker\n")
            if kind == "mem":
                ir.append("  %flag = call i8 @llvm.x86.arpl.mem(ptr %p, i16 %s)\n")
            else:
                source = "%d" if kind == "alias" else "%s"
                ir.append(f"  %pair = call {{ i16, i8 }} @llvm.x86.arpl.reg(i16 %d, i16 {source})\n"
                          "  %value = extractvalue { i16, i8 } %pair, 0\n"
                          "  %flag = extractvalue { i16, i8 } %pair, 1\n")
            ir.append("  store i8 29, ptr %marker\n")
            if results & 1:
                ir.append("  %wide = zext i16 %value to i32\n")
            if results & 2:
                ir.append("  %zf = zext i8 %flag to i32\n"
                          "  %shift = shl i32 %zf, 16\n")
            if results == 3:
                ir.append("  %both = or i32 %wide, %shift\n")
            ir.append(f"  ret i32 {('42', '%wide', '%shift', '%both')[results]}\n}}\n")
    ir.append('attributes #0 = { "target-features"="-sse,-sse2,-mmx" }\n')
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
    (output / "guest.ld").write_text("""ENTRY(probe_reg_0)
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
         ROOT / "protected_arpl_kvm.c", "-o", runner], "host-build")
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
            r"^([0-9a-f]+) <(probe_(?:reg|alias|mem)_[0-3])>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
            assembly, re.M | re.S)
        assert len(functions) == 8, assembly
        manifest = []
        first_load = first_status = None
        for address, name, body in functions:
            kind, results = re.fullmatch(r"probe_(reg|alias|mem)_([0-3])", name).groups()
            instructions = re.findall(
                r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            accesses = [(int(pc, 16), re.fullmatch(r"arpl\s+%(\w+), (.*)", text))
                        for pc, text in instructions if re.match(r"arpl\b", text)]
            assert len(accesses) == 1 and accesses[0][1], body
            pc, match = accesses[0]
            source, destination = match.groups()
            source_index = WORDS.index(source)
            if kind == "mem":
                base = re.fullmatch(r"\(%(\w+)\)", destination)
                assert base, body
                destination_index = REGISTERS.index(base[1])
            else:
                assert destination.startswith("%"), body
                destination_index = WORDS.index(destination[1:])
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            manifest.append(f"{name} {address} {pc:x} {int(kind == 'mem')} {int(kind == 'alias')} {results} {destination_index} {source_index}")
            if first_load is None:
                moves = [(int(address, 16), int(instructions[i + 1][0], 16))
                         for i, (address, text) in enumerate(instructions[:-1])
                         if int(address, 16) < pc and re.fullmatch(
                             r"mov(?:w|l|zwl)\s+[^,]+, %(?:" + source + "|" + REGISTERS[source_index] + ")", text)]
                assert moves, body
                first_load = (*moves[-1], source_index)
            if first_status is None and int(results) & 2:
                setters = [int(address, 16) for address, text in instructions
                           if re.match(r"sete\b", text)]
                assert len(setters) == 1, body
                first_status = setters[0]
        manifest_path = output / f"{level}.manifest"
        manifest_path.write_text("\n".join(manifest) + "\n")
        native = json.loads(run([runner, binary, manifest_path, args.mode], level + "-native"))
        assert native["complete_state_pairs"] == (6 + 2 * 12) * 2 * 4 * 16 * 2, native
        assert all(native[key] for key in ("returns", "changed", "unchanged", "gp", "pf", "ac")), native
        # Keep ARPL intact, but corrupt the declared source transported into it.
        damaged = bytearray(binary.read_bytes())
        start, end, source = first_load
        mutation = (b"\x66" if args.mode == 16 else b"") + bytes((0x31, 0xc0 | source << 3 | source))
        assert end - start >= len(mutation)
        damaged[start - 0x10000:end - 0x10000] = mutation + b"\x90" * (end - start - len(mutation))
        negative = output / f"{level}-wrong-source.bin"
        negative.write_bytes(damaged)
        run([runner, negative, manifest_path, args.mode], level + "-wrong-source", expected=1)
        assert "gpr(boundary, source)" in (output / f"{level}-wrong-source.stderr").read_text()
        # Native boundary equality is insufficient: invert the returned flag
        # after the real ARPL and require the compiler-result check to reject it.
        damaged = bytearray(binary.read_bytes())
        offset = first_status - 0x10000
        assert damaged[offset:offset + 2] == b"\x0f\x94"
        damaged[offset + 1] = 0x95
        negative = output / f"{level}-wrong-status.bin"
        negative.write_bytes(damaged)
        run([runner, negative, manifest_path, args.mode], level + "-wrong-status", expected=1)
        assert "returned.rax == output" in (output / f"{level}-wrong-status.stderr").read_text()
        profiles.append(dict(optimization=level, mode=args.mode,
                             image_sha256=sha256(binary), manifest_sha256=sha256(manifest_path),
                             negative_controls=["wrong source before intact ARPL rejected",
                                                "inverted status after intact ARPL rejected"], **native))
        print(f"PASS code{args.mode} {level}: {native}", flush=True)
    report = dict(schema_version=1, tools={k: sha256(v) for k, v in tools.items()},
                  inputs={str(p.relative_to(ROOT)): sha256(p) for p in
                          [Path(__file__), ROOT / "protected_arpl_kvm.c",
                           ROOT / "KvmTransferFixture.h", ROOT / "KvmProtectedFaultFixture.h"]},
                  profiles=profiles,
                  comparisons=sum(p["complete_state_pairs"] for p in profiles),
                  boundary="CPL0/CPL3 protected code16/code32, 32-bit pointer/stack ABI; original ARPL register and memory effects, flags, #GP/#PF/#AC frames, read-only/missing/cross-page operands and explicit DS cache states. ABI-compatible successes return through the compiler frame. No NeverC i386 frontend, real/VM86, asynchronous events or device memory.")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

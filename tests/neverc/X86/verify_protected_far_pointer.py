#!/usr/bin/env python3
"""Qualify native IR/C far-pointer loads in protected code16/code32 using KVM."""

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
OPERATIONS = ("lds", "les", "lss", "lfs", "lgs")


def sources():
    ir = []
    for operation in OPERATIONS:
        for bits in (16, 32):
            ir.append(f"declare i{bits} @llvm.x86.{operation}{bits}(ptr)\n")
            for used in (0, 1):
                ir.append(f"define i32 @probe_{operation}{bits}_{used}(ptr %p, ptr %marker) #0 {{\n"
                          "  store i8 17, ptr %marker\n"
                          f"  %value = call i{bits} @llvm.x86.{operation}{bits}(ptr %p)\n"
                          "  store i8 29, ptr %marker\n")
                if used and bits == 16:
                    ir.append("  %wide = zext i16 %value to i32\n")
                value = ("%wide" if bits == 16 else "%value") if used else "42"
                ir.append(f"  ret i32 {value}\n}}\n")
    ir.append('attributes #0 = { "target-features"="-sse,-sse2,-mmx" }\n')
    return "".join(ir)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def c_sources():
    source = []
    for operation in OPERATIONS:
        for bits in (16, 32):
            for used in (0, 1):
                source.append(
                    f"unsigned probe_{operation}{bits}_{used}(const void *p, "
                    "unsigned char *marker) {\n"
                    "  *marker = 17;\n"
                    f"  unsigned value = __builtin_ia32_{operation}{bits}(p);\n"
                    "  *marker = 29;\n"
                    f"  return {'value' if used else '42'};\n}}\n"
                )
    return "".join(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--mode", type=int, choices=(16, 32), required=True)
    parser.add_argument("--frontend", type=Path,
                        help="compile builtin C through this Clang or NeverC")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(output))
    tools = {name: shutil.which(name) for name in ("clang", "ld.lld", "objcopy")}
    if not all(tools.values()):
        parser.error("clang, ld.lld and objcopy are required")
    tools.update({name: str((args.llvm_bin / name).resolve())
                  for name in ("llc", "opt", "llvm-objdump")})
    if args.frontend:
        tools["frontend"] = str(args.frontend.resolve())
    tool_hashes = {name: sha256(path) for name, path in tools.items()}
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
    (output / "input.c").write_text(c_sources())
    (output / "guest.ld").write_text("""ENTRY(probe_lds16_0)
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
         ROOT / "protected_far_pointer_kvm.c", "-o", runner], "host-build")
    profiles = []
    for level in ("O0", "O2"):
        source = output / "input.ll"
        triple = "i386-unknown-linux-" + ("code16" if args.mode == 16 else "gnu")
        if args.frontend:
            source = output / f"{level}-frontend.ll"
            run([tools["frontend"], f"--target={triple}", f"-{level}",
                 "-fno-pic", "-fno-pie", "-fno-stack-protector",
                 "-mno-sse", "-mno-sse2", "-mno-mmx", "-S", "-emit-llvm",
                 output / "input.c", "-o", source], level + "-frontend")
            text = source.read_text()
            assert "asm " not in text and "memory(none)" not in text, source
            for operation in OPERATIONS:
                for bits in (16, 32):
                    assert text.count(f"call i{bits} @llvm.x86.{operation}{bits}(ptr") == 2, source
        if level == "O2":
            optimized = output / "optimized.ll"
            run([tools["opt"], "-S", "-passes=default<O2>", source,
                 "-o", optimized], level + "-opt")
            source = optimized
        obj, elf, binary = (output / (level + suffix)
                            for suffix in (".o", ".elf", ".bin"))
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
            r"^([0-9a-f]+) <(probe_l[defgs]s(?:16|32)_[01])>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
            assembly, re.M | re.S)
        assert len(functions) == 20, assembly
        manifest = []
        first_load = None
        for address, name, body in functions:
            operation, bits, used = re.fullmatch(
                r"probe_(l[defgs]s)(16|32)_([01])", name).groups()
            instructions = re.findall(
                r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            loads = [(int(pc, 16), re.fullmatch(
                r"(l[defgs]s)([wl])\s+\(%(\w+)\), %(\w+)", text))
                for pc, text in instructions if re.match(r"l[defgs]s[wl]\b", text)]
            assert len(loads) == 1 and loads[0][1], body
            pc, match = loads[0]
            mnemonic, suffix, base, destination = match.groups()
            assert mnemonic == operation and suffix == ("w" if bits == "16" else "l"), body
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            base_index = REGISTERS.index(base)
            dest_index = (WORDS if bits == "16" else REGISTERS).index(destination)
            manifest.append(f"{name} {address} {pc:x} {OPERATIONS.index(operation)} {bits} {used} {base_index} {dest_index}")
            if first_load is None:
                moves = [(int(address, 16), int(instructions[i + 1][0], 16))
                         for i, (address, text) in enumerate(instructions[:-1])
                         if int(address, 16) < pc and
                         re.fullmatch(r"movl\s+[^,]+, %" + base, text)]
                assert moves, body
                first_load = (*moves[-1], base_index)
        manifest_path = output / f"{level}.manifest"
        manifest_path.write_text("\n".join(manifest) + "\n")
        native = json.loads(run([runner, binary, manifest_path, args.mode], level + "-native"))
        assert native["complete_state_pairs"] == 20 * 19 * 7 * 6, native
        assert all(native[key] for key in ("returns", "np", "ss", "gp", "pf")), native
        # Preserve the far load, but feed it zero instead of the declared
        # pointer. Boundary validation must reject this transport corruption.
        damaged = bytearray(binary.read_bytes())
        start, end, base = first_load
        mutation = (b"\x66" if args.mode == 16 else b"") + bytes((0x31, 0xc0 | base << 3 | base))
        assert end - start >= len(mutation)
        damaged[start - 0x10000:end - 0x10000] = mutation + b"\x90" * (end - start - len(mutation))
        negative = output / f"{level}-wrong-pointer.bin"
        negative.write_bytes(damaged)
        run([runner, negative, manifest_path, args.mode], level + "-wrong-pointer", expected=1)
        assert "gpr(boundary, base) == pointer" in (output / f"{level}-wrong-pointer.stderr").read_text()
        profiles.append(dict(optimization=level, mode=args.mode,
                             image_sha256=sha256(binary), manifest_sha256=sha256(manifest_path),
                             negative_control="wrong pointer before intact far load rejected", **native))
        print(f"PASS code{args.mode} {level}: {native}", flush=True)
    assert tool_hashes == {name: sha256(path) for name, path in tools.items()}, "compiler changed during qualification"
    report = dict(schema_version=2, tools=tool_hashes,
                  frontend=str(args.frontend.resolve()) if args.frontend else None,
                  inputs={str(p.relative_to(ROOT)): sha256(p) for p in
                          [Path(__file__), ROOT / "protected_far_pointer_kvm.c",
                           ROOT / "KvmTransferFixture.h", ROOT / "KvmProtectedFaultFixture.h"]},
                  profiles=profiles,
                  comparisons=sum(p["complete_state_pairs"] for p in profiles),
                  boundary="CPL0 protected code16/code32 with a 32-bit stack/pointer ABI; native offset/segment loads, descriptor effects and #GP/#NP/#SS/#PF frames. Original fault handlers expose their vector with OUT. ABI-compatible successful cases also return through the compiler frame. Frontend C, when selected, is compiled through the full LLVM backend. No embedded NeverC i386 backend, real/VM86, CPL3 #AC, MOV-SS shadows or asynchronous events.")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

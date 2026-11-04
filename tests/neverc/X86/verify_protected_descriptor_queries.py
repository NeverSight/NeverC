#!/usr/bin/env python3
"""Qualify explicit-address LAR/LSL in protected code16/code32 using original KVM execution."""

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
SEGMENTS = {0: None, 0x26: "es", 0x2e: "cs", 0x36: "ss", 0x3e: "ds", 0x64: "fs", 0x65: "gs"}


def sources():
    text = []
    for operation in ("lar", "lsl"):
        for bits in (16, 32):
            result = "{ i" + str(bits) + ", i8 }"
            api = f"llvm.x86.{operation}.mem{bits}.address32"
            text.append(f"declare {result} @{api}(i32, i32 immarg, i32 immarg, i{bits})\n")
            for address_bits in (16, 32):
                for prefix in SEGMENTS:
                    for used in range(4):
                        name = f"probe_{operation}_{bits}_{address_bits}_{prefix}_{used}"
                        body = [f"define i64 @{name}(i32 %offset, i32 %prior, ptr %marker) #0 {{",
                                "  store i8 17, ptr %marker"]
                        prior = "%prior"
                        if bits == 16:
                            body.append("  %narrow = trunc i32 %prior to i16")
                            prior = "%narrow"
                        body.append(f"  %pair = call {result} @{api}(i32 %offset, i32 {address_bits}, i32 {prefix}, i{bits} {prior})")
                        body.append("  store i8 29, ptr %marker")
                        value, status = "42", "0"
                        if used & 1:
                            body += [f"  %value = extractvalue {result} %pair, 0",
                                     f"  %wide = zext i{bits} %value to i64"]
                            value = "%wide"
                        if used & 2:
                            body += [f"  %zf = extractvalue {result} %pair, 1",
                                     "  %flag = zext i8 %zf to i64",
                                     "  %status = shl i64 %flag, 32"]
                            status = "%status"
                        body += [f"  %result = or i64 {value}, {status}", "  ret i64 %result", "}\n"]
                        text.append("\n".join(body))
    text.append('attributes #0 = { "target-features"="-sse,-sse2,-mmx" }\n')
    return "".join(text)


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
        result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=timeout)
        (output / f"{stem}.stdout").write_text(result.stdout)
        (output / f"{stem}.stderr").write_text(result.stderr)
        records.append(dict(command=command, exit_code=result.returncode,
                            seconds=time.monotonic()-started,
                            stdout=f"{stem}.stdout", stderr=f"{stem}.stderr"))
        (output / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{stem}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    (output / "input.ll").write_text(sources())
    (output / "guest.ld").write_text("""ENTRY(probe_lar_16_16_0_0)
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
         ROOT / "protected_descriptor_query_kvm.c", "-o", runner], "host-build")
    profiles = []
    for level in ("O0", "O2"):
        source = output / "input.ll"
        if level == "O2":
            optimized = output / "optimized.ll"
            run([tools["opt"], "-S", "-passes=default<O2>", source, "-o", optimized], level+"-opt")
            source = optimized
        obj, elf, binary = (output / (level+suffix) for suffix in (".o", ".elf", ".bin"))
        triple = "i386-unknown-linux-" + ("code16" if args.mode == 16 else "gnu")
        run([tools["llc"], f"-mtriple={triple}", f"-{level}",
             *(["-enable-new-pm"] if level == "O2" else []), "-verify-machineinstrs",
             "-filetype=obj", source, "-o", obj], level+"-compile")
        run([tools["ld.lld"], "-m", "elf_i386", "-T", output / "guest.ld", obj, "-o", elf], level+"-link")
        run([tools["objcopy"], "-O", "binary", elf, binary], level+"-image")
        assembly = run([tools["llvm-objdump"], "-d",
                        *(["--mattr=+16bit-mode,-32bit-mode"] if args.mode == 16 else []),
                        elf], level+"-assembly")
        functions = re.findall(r"^([0-9a-f]+) <(probe_(?:lar|lsl)_\d+_\d+_\d+_[0-3])>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)", assembly, re.M | re.S)
        assert len(functions) == 224, assembly
        manifest = []
        first_offset = first_status = None
        for address, name, body in functions:
            operation, bits, address_bits, prefix, used = re.fullmatch(
                r"probe_(lar|lsl)_(\d+)_(\d+)_(\d+)_([0-3])", name).groups()
            bits, address_bits, prefix, used = map(int, (bits, address_bits, prefix, used))
            instructions = re.findall(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            queries = [(i, re.fullmatch(r"(?:lar|lsl)([wl])\s+(.*), %(\w+)", text))
                       for i, (_, text) in enumerate(instructions) if re.match(r"(?:lar|lsl)[wl]\s", text)]
            assert len(queries) == 1 and queries[0][1], body
            index, query = queries[0]
            size, memory, destination = query.groups()
            assert size == ("w" if bits == 16 else "l"), body
            source = re.fullmatch(r"(?:(%\w+):)?\(%(\w+)\)", memory)
            assert source, body
            assert source[1] == ("%"+SEGMENTS[prefix] if prefix else None), body
            register = source[2]
            base = (WORDS if address_bits == 16 else REGISTERS).index(register)
            dest = (WORDS if bits == 16 else REGISTERS).index(destination)
            pc, next_pc = int(instructions[index][0], 16), int(instructions[index+1][0], 16)
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            manifest.append(f"{name} {address} {pc:x} {next_pc:x} {int(operation=='lsl')} {bits} {address_bits} {prefix} {used} {dest} {base}")
            if first_offset is None:
                loads = [(int(address, 16), int(instructions[i+1][0], 16))
                         for i, (address, text) in enumerate(instructions[:index])
                         if re.fullmatch(r"mov(?:l|w|zwl)\s+[^,]+, %(?:"+REGISTERS[base]+"|"+WORDS[base]+")", text)]
                assert loads, body
                first_offset = (*loads[-1], base, address_bits)
            if first_status is None and used == 3:
                statuses = [int(address, 16) for address, text in instructions[index+1:]
                            if re.match(r"sete\s", text)]
                assert len(statuses) == 1, body
                first_status = statuses[0]
        manifest_path = output / (level+".manifest")
        manifest_path.write_text("\n".join(manifest)+"\n")
        native = json.loads(run([runner, binary, manifest_path, args.mode], level+"-native"))
        assert native["complete_state_pairs"] == 143360, native
        assert all(native[k] for k in ("returns", "success", "rejected", "pf", "ac")), native
        for label in ("offset", "status"):
            damaged = bytearray(binary.read_bytes())
            if label == "offset":
                start, end, base, address_bits = first_offset
                # Zero the full carrier even for an addr16 query. Matching the
                # compiler's 32-bit register copy also fits its code32 extent.
                mutation = (b"\x66" if args.mode == 16 else b"") + bytes((0x31, 0xc0 | base << 3 | base))
                assert end-start >= len(mutation)
                damaged[start-0x10000:end-0x10000] = mutation + b"\x90"*(end-start-len(mutation))
                needle = "== effective"
            else:
                index = first_status-0x10000
                assert damaged[index:index+2] == b"\x0f\x94"
                damaged[index+1] = 0x95
                needle = "result == want"
            negative = output / f"{level}-wrong-{label}.bin"
            negative.write_bytes(damaged)
            run([runner, negative, manifest_path, args.mode], level+"-wrong-"+label, expected=1)
            assert needle in (output/(level+"-wrong-"+label+".stderr")).read_text()
        profiles.append(dict(optimization=level, mode=args.mode, image_sha256=sha256(binary),
                             manifest_sha256=sha256(manifest_path),
                             negative_controls=["wrong declared offset before intact query rejected",
                                                "inverted returned ZF after intact query rejected"], **native))
        print(f"PASS code{args.mode} {level}: {native}", flush=True)
    record = dict(schema_version=1, tools={k:sha256(v) for k,v in tools.items()},
                  inputs={str(p.relative_to(ROOT)):sha256(p) for p in
                          [Path(__file__), ROOT/"protected_descriptor_query_kvm.c",
                           ROOT/"KvmTransferFixture.h", ROOT/"KvmProtectedFaultFixture.h"]},
                  profiles=profiles, comparisons=sum(p["complete_state_pairs"] for p in profiles),
                  boundary="CPL0/CPL3 protected code16/code32, 16/32-bit addresses/results, every segment prefix, nonflat FS/GS, used/dead/value/status results, selector privilege/type/limit rejection, source/descriptor paging and alignment faults. Complete represented state and RAM are compared with independent original bytes; successes validate the compiler's prior/value/ZF and return ABI. No NeverC i386 frontend, real/VM86, asynchronous events or arbitrary invalid segment caches.")
    (output/"qualification.json").write_text(json.dumps(record,indent=2)+"\n")


if __name__ == "__main__":
    main()

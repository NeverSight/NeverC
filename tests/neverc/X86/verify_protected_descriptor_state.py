#!/usr/bin/env python3
"""Qualify explicit-address descriptor-state instructions in protected code16/code32 using original KVM execution."""

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


OPERATIONS = (
    "verr", "verw", "sldt", "str", "smsw", "lldt", "ltr", "lmsw",
    "sgdt", "sidt", "lgdt", "lidt", "invlpg", "sgdt16", "sidt16", "lgdt16", "lidt16")


def sources():
    text = []
    for operation, name in enumerate(OPERATIONS):
        verify = operation < 2
        api = "llvm.x86." + name + (".mem" if operation < 8 else "") + ".address32"
        result = "i8" if verify else "void"
        text.append(f"declare {result} @{api}(i32, i32 immarg, i32 immarg)\n")
        for address_bits in (16, 32):
            for prefix in SEGMENTS:
                for used in ((0, 1) if verify else (0,)):
                    function = f"probe_{name}_{address_bits}_{prefix}_{used}"
                    body = [f"define i32 @{function}(i32 %offset, ptr %marker) #0 {{",
                            "  store volatile i8 17, ptr %marker"]
                    body += [("  %query = " if verify else "  ") +
                             f"call {result} @{api}(i32 %offset, i32 {address_bits}, i32 {prefix})",
                             "  store volatile i8 29, ptr %marker"]
                    if verify and used:
                        body += ["  %flag = zext i8 %query to i32", "  ret i32 %flag"]
                    else:
                        body += ["  ret i32 42"]
                    text.append("\n".join(body + ["}\n"]))
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
    (output / "guest.ld").write_text("""ENTRY(probe_verr_16_0_0)
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
         ROOT / "protected_descriptor_state_kvm.c", "-o", runner], "host-build")
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
        functions = re.findall(r"^([0-9a-f]+) <(probe_[a-z0-9]+_\d+_\d+_[01])>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)", assembly, re.M | re.S)
        assert len(functions) == 266, assembly
        manifest = []
        first_offset = first_status = None
        for address, name, body in functions:
            namepart, address_bits, prefix, used = re.fullmatch(
                r"probe_([a-z0-9]+)_(\d+)_(\d+)_([01])", name).groups()
            operation = OPERATIONS.index(namepart)
            address_bits, prefix, used = map(int, (address_bits, prefix, used))
            instructions = re.findall(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            mnemonic = namepart.removesuffix("16")
            queries = [(i, re.fullmatch(mnemonic + r"[wl]?\s+(.*)", text))
                       for i, (_, text) in enumerate(instructions)
                       if re.match(mnemonic + r"[wl]?\s", text)]
            assert len(queries) == 1 and queries[0][1], body
            index, query = queries[0]
            source = re.fullmatch(r"(?:(%\w+):)?\(%(\w+)\)", query[1])
            assert source, body
            assert source[1] == ("%"+SEGMENTS[prefix] if prefix else None), body
            register = source[2]
            base = (WORDS if address_bits == 16 else REGISTERS).index(register)
            pc, next_pc = int(instructions[index][0], 16), int(instructions[index+1][0], 16)
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            manifest.append(f"{name} {address} {pc:x} {next_pc:x} {operation} {address_bits} {prefix} {used} {base}")
            if first_offset is None:
                loads = [(int(address, 16), int(instructions[i+1][0], 16))
                         for i, (address, text) in enumerate(instructions[:index])
                         if re.fullmatch(r"mov(?:l|w|zwl)\s+[^,]+, %(?:"+REGISTERS[base]+"|"+WORDS[base]+")", text)]
                assert loads, body
                first_offset = (*loads[-1], base, address_bits)
            if first_status is None and used == 1:
                statuses = [int(address, 16) for address, text in instructions[index+1:]
                            if re.match(r"sete\s", text)]
                assert len(statuses) == 1, body
                first_status = statuses[0]
        manifest_path = output / (level+".manifest")
        manifest_path.write_text("\n".join(manifest)+"\n")
        native = json.loads(run([runner, binary, manifest_path, args.mode], level+"-native"))
        assert native["complete_state_pairs"] == 99456, native
        assert all(native[k] for k in ("returns", "success", "rejected", "np", "gp", "pf", "ac")), native
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
                                                "inverted returned ZF after intact verification rejected"], **native))
        print(f"PASS code{args.mode} {level}: {native}", flush=True)
    record = dict(schema_version=1, tools={k:sha256(v) for k,v in tools.items()},
                  inputs={str(p.relative_to(ROOT)):sha256(p) for p in
                          [Path(__file__), ROOT/"protected_descriptor_state_kvm.c",
                           ROOT/"KvmTransferFixture.h", ROOT/"KvmProtectedFaultFixture.h"]},
                  profiles=profiles, comparisons=sum(p["complete_state_pairs"] for p in profiles),
                  boundary="CPL0/CPL3 protected code16/code32, 17 explicit-address state APIs, 16/32-bit addresses, all segment prefixes, nonflat CS/FS/GS, UMIP, table/selector inputs, used/dead ZF, descriptor busy writes and source/descriptor faults. Complete represented GPR/segment/system/event/RAM state is compared with original bytes. INVLPG encoding, transport and exceptions are checked; no physical stale-TLB experiment is claimed. No NeverC i386 frontend, real/VM86, asynchronous events or arbitrary invalid caches.")
    (output/"qualification.json").write_text(json.dumps(record,indent=2)+"\n")


if __name__ == "__main__":
    main()

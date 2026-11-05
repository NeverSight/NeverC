#!/usr/bin/env python3
"""Compile actual NeverD ARPL exports and compare original native state in KVM.

Generate X86ArplSource exports using NEVERD_ISA_REGRESSION_ARTIFACT_DIR. Each
translation unit stays separate, retaining both writers' own helper types.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from verify_protected_arpl import REGISTERS, WORDS

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exports", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--frontend", type=Path, required=True)
    parser.add_argument("--fallback", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = {name: shutil.which(name) for name in ("clang", "ld.lld", "objcopy")}
    assert all(tools.values())
    tools.update(
        {
            name: str((args.llvm_bin / name).resolve())
            for name in ("llc", "llvm-objdump")
        }
    )
    tools["frontend"] = str(args.frontend.resolve())
    records, profiles, hashes = [], [], {}

    def run(command, stem, expected=0):
        command = list(map(str, command))
        result = subprocess.run(
            command,
            text=True,
            capture_output=True,
            timeout=600,
            env=dict(os.environ, TMPDIR=str(output)),
        )
        (output / f"{stem}.stdout").write_text(result.stdout)
        (output / f"{stem}.stderr").write_text(result.stderr)
        records.append(dict(command=command, exit_code=result.returncode, log=stem))
        (output / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{stem}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    (output / "guest.ld").write_text("""ENTRY(probe)
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
    run(
        [
            tools["clang"],
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            ROOT / "protected_arpl_kvm.c",
            "-o",
            runner,
        ],
        "host-build",
    )
    exports = sorted(args.exports.glob("arpl_*.c"))
    assert len(exports) == 48, len(exports)
    objects = sorted(
        path
        for path in args.exports.glob("arpl_*.o")
        if path.read_bytes()[:4] == b"\x7fELF"
    )
    assert len(objects) == 8, len(objects)
    exports += objects
    negative = set()
    for path in exports:
        match = re.fullmatch(
            r"arpl_(\d+)_(\d+)_([0-3])\.(high\.c|llvm\.c|o)", path.name
        )
        assert match, path
        fmt, noopt, kind, route = match.groups()
        kind = int(kind)
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        name = path.name.replace(".", "_")
        source = output / (name + ".c")
        object_input = path.suffix == ".o"
        text = (
            "extern unsigned virtualization_test(unsigned, unsigned);\n"
            if object_input
            else path.read_text()
        )
        assert object_input or re.search(
            r"\b(?:u)?int32_t virtualization_test\(", text
        ), text
        text += (
            "\nunsigned probe(unsigned a, unsigned b, void *p, volatile unsigned char *m) { "
            "*m=17; unsigned r=virtualization_test("
            + ("(unsigned)p" if kind else "a")
            + ",b); *m=29; return r; }\n"
        )
        source.write_text(text)
        for level in ("O0", "O2"):
            stem = name + "-" + level
            ir, obj, elf, binary = (
                output / (stem + suffix) for suffix in (".ll", ".o", ".elf", ".bin")
            )
            run(
                [
                    tools["frontend"],
                    "--target=i386-unknown-linux-gnu",
                    "-" + level,
                    "-fno-pic",
                    "-fno-pie",
                    "-fno-stack-protector",
                    "-mno-sse",
                    "-mno-sse2",
                    "-mno-mmx",
                    "-S",
                    "-emit-llvm",
                    source,
                    "-o",
                    ir,
                ],
                stem + "-frontend",
            )
            llvm = ir.read_text()
            if not object_input:
                assert ("@llvm.x86.arpl." in llvm) != args.fallback, ir
                assert (" asm " in llvm) == args.fallback, ir
            run(
                [
                    tools["llc"],
                    "-mtriple=i386-unknown-linux-gnu",
                    "-" + level,
                    "-verify-machineinstrs",
                    "-filetype=obj",
                    ir,
                    "-o",
                    obj,
                ],
                stem + "-compile",
            )
            run(
                [
                    tools["ld.lld"],
                    "-m",
                    "elf_i386",
                    "-T",
                    output / "guest.ld",
                    obj,
                    *([path] if object_input else []),
                    "-o",
                    elf,
                ],
                stem + "-link",
            )
            run([tools["objcopy"], "-O", "binary", elf, binary], stem + "-image")
            assembly = run([tools["llvm-objdump"], "-d", elf], stem + "-assembly")
            entry = re.search(r"^([0-9a-f]+) <probe>:", assembly, re.M)
            assert entry, assembly
            # O2 may inline the export into the wrapper; qualify the wrapper's
            # ARPL if present, otherwise the separately called exported body.
            functions = dict(
                (name, (address, body))
                for address, name, body in re.findall(
                    r"^([0-9a-f]+) <([^>]+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
                    assembly,
                    re.M | re.S,
                )
            )
            body = functions["probe"][1]
            if not re.search(r"\barpl\b", body):
                body = functions["virtualization_test"][1]
            instructions = re.findall(
                r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M
            )
            accesses = [
                (int(pc, 16), re.fullmatch(r"arpl\s+%(\w+), (.*)", text))
                for pc, text in instructions
                if re.match(r"arpl\b", text)
            ]
            assert len(accesses) == 1 and accesses[0][1], body
            pc, access = accesses[0]
            src, dst = access.groups()
            source_index = WORDS.index(src)
            prefix = (0, 0, 0x64, 0x65)[kind]
            if kind:
                base = re.fullmatch(
                    (r"%fs:" if kind == 2 else r"%gs:" if kind == 3 else "")
                    + r"\(%(\w+)\)",
                    dst,
                )
                assert base, body
                destination_index = REGISTERS.index(base[1])
            else:
                destination_index = WORDS.index(dst[1:])
            manifest = output / (stem + ".manifest")
            manifest.write_text(
                f"{name} {entry[1]} {pc:x} {int(bool(kind))} 0 {2 if kind else 3} {destination_index} {source_index} 32 {prefix}\n"
            )
            result = json.loads(run([runner, binary, manifest, 32], stem + "-native"))
            assert (
                result["complete_state_pairs"] == (12 if kind else 1) * 2 * 4 * 16 * 2
            )
            if level not in negative and not kind:
                setters = [
                    int(address, 16)
                    for address, text in instructions
                    if re.match(r"sete\b", text)
                ]
                assert len(setters) == 1, body
                damaged = bytearray(binary.read_bytes())
                offset = setters[0] - 0x10000
                assert damaged[offset : offset + 2] == b"\x0f\x94"
                damaged[offset + 1] = 0x95
                bad = output / (stem + "-wrong-status.bin")
                bad.write_bytes(damaged)
                run([runner, bad, manifest, 32], stem + "-wrong-status", expected=1)
                assert (
                    "returned.rax == output"
                    in (output / (stem + "-wrong-status.stderr")).read_text()
                )
                negative.add(level)
            profiles.append(
                dict(
                    source=path.name,
                    optimization=level,
                    image_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    **result,
                )
            )
            print(f"PASS {stem}: {result['complete_state_pairs']}", flush=True)
    assert len(negative) == 2
    report = dict(
        schema_version=1,
        inputs={
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [
                Path(__file__),
                ROOT / "protected_arpl_kvm.c",
                ROOT / "verify_protected_arpl.py",
                ROOT / "KvmProtectedFaultFixture.h",
                ROOT / "KvmTransferFixture.h",
            ]
        },
        exports=hashes,
        tools={
            name: hashlib.sha256(Path(path).read_bytes()).hexdigest()
            for name, path in tools.items()
        },
        fallback=args.fallback,
        profiles=profiles,
        comparisons=sum(p["complete_state_pairs"] for p in profiles),
        negative_controls="Intact ARPL with inverted returned ZF rejected at O0/O2",
        boundary="Actual NeverD HighC/LLVMC exports from ELF/COFF/MachO recompiled for i386 SysV32; native instruction, complete GPR/flags/system/event/RAM and original #GP/#PF/#AC frames against independent bytes; ordinary pointer DS and nonflat FS/GS. No original-image fault dispatcher, real/VM86 or asynchronous events.",
    )
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

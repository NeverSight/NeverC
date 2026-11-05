#!/usr/bin/env python3
"""Compare full LLVM BOUND in protected code16/code32 with original KVM bytes."""

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
REGS = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
WORDS = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")


def sources():
    ir = []
    for width in (2, 4):
        ty = f"i{width * 8}"
        native = f"llvm.x86.bound{width * 8}.address32"
        ir.append(f"declare void @{native}({ty}, i32, i32 immarg, i32 immarg)")
        for bits, prefix in ((32, 0), (32, 0x64), (16, 0x64)):
            for dead in (0, 1):
                name = f"probe_{width}_{bits}_{prefix}_{dead}"
                ir.append(
                    f"define i32 @{name}(i32 %index, i32 %offset, ptr %marker) #0 {{"
                )
                if width == 2:
                    ir.append("%word = trunc i32 %index to i16")
                ir.append("store i8 17, ptr %marker")
                index = "%word" if width == 2 else "%index"
                ir.append(
                    f"call void @{native}({ty} {index}, i32 %offset, i32 {bits}, i32 {prefix})"
                )
                ir.append("store i8 29, ptr %marker")
                ir.append("ret i32 " + ("42" if dead else "%index") + "\n}")
    ir.append('attributes #0 = { "target-features"="-sse,-sse2,-mmx" }')
    return "\n".join(ir)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--llvm-bin", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--mode", type=int, choices=(16, 32), required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tools = {n: shutil.which(n) for n in ("clang", "ld.lld", "objcopy")}
    assert all(tools.values()), "clang, ld.lld and objcopy are required"
    tools.update(
        {n: str((a.llvm_bin / n).resolve()) for n in ("llc", "opt", "llvm-objdump")}
    )
    records = []

    def run(command, name, expected=0):
        command = list(map(str, command))
        start = time.monotonic()
        r = subprocess.run(command, text=True, capture_output=True, timeout=600)
        (out / (name + ".stdout")).write_text(r.stdout)
        (out / (name + ".stderr")).write_text(r.stderr)
        records.append(
            dict(
                command=command,
                exit_code=r.returncode,
                seconds=time.monotonic() - start,
                log=name,
            )
        )
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if r.returncode != expected:
            raise RuntimeError(f"{name}: exit {r.returncode}\n{r.stderr}")
        return r.stdout

    (out / "input.ll").write_text(sources())
    (out / "guest.ld").write_text(
        "ENTRY(probe_2_32_0_0)\nSECTIONS { . = 0x10000; .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) } /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) } }\n"
    )
    runner = out / "kvm-runner"
    run(
        [
            tools["clang"],
            "-std=c11",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            ROOT / "protected_bound_kvm.c",
            "-o",
            runner,
        ],
        "host-build",
    )
    triple = "i386-unknown-linux-" + ("code16" if a.mode == 16 else "gnu")
    profiles = []

    def compile_run(source, stem, level, expected=0):
        obj, elf, binary = (out / (stem + ext) for ext in (".o", ".elf", ".bin"))
        run(
            [
                tools["llc"],
                f"-mtriple={triple}",
                "-" + level,
                "-verify-machineinstrs",
                "-filetype=obj",
                source,
                "-o",
                obj,
            ],
            stem + "-compile",
        )
        run(
            [tools["ld.lld"], "-m", "elf_i386", "-T", out / "guest.ld", obj, "-o", elf],
            stem + "-link",
        )
        run([tools["objcopy"], "-O", "binary", elf, binary], stem + "-image")
        disasm = run(
            [
                tools["llvm-objdump"],
                "-d",
                *(["--mattr=+16bit-mode,-32bit-mode"] if a.mode == 16 else []),
                elf,
            ],
            stem + "-assembly",
        )
        functions = re.findall(
            r"^([0-9a-f]+) <(probe_\d+_\d+_\d+_\d)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
            disasm,
            re.M | re.S,
        )
        assert len(functions) == 12
        manifest = []
        for entry, name, body in functions:
            width, bits, prefix, dead = map(int, name.split("_")[1:])
            bounds = re.findall(
                r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*bound\s+%(\w+), (?:%(\w+):)?\(%(\w+)\)",
                body,
                re.M,
            )
            assert len(bounds) == 1, body
            pc, index, segment, base = bounds[0]
            assert segment == ("fs" if prefix else ""), body
            index_reg = (WORDS if width == 2 else REGS).index(index)
            base_reg = (WORDS if bits == 16 else REGS).index(base)
            assert not re.search(r"%[xyz]mm\d|%mm\d|%st\b|\bcall[lw]?\b", body), body
            manifest.append(
                f"{name} {entry} {pc} {width} {bits} {prefix} {dead} {index_reg} {base_reg}"
            )
        path = out / (stem + ".manifest")
        path.write_text("\n".join(manifest) + "\n")
        output = run([runner, binary, path, a.mode], stem + "-native", expected)
        if not expected:
            result = json.loads(output)
            assert result["complete_state_pairs"] == 12 * 2 * 9 * 2 * 10
            assert all(
                result[k] for k in ("returns", "completions", "br", "gp", "pf", "ac")
            )
            profiles.append(
                dict(
                    mode=a.mode,
                    optimization=level,
                    image_sha256=sha256(binary),
                    manifest_sha256=sha256(path),
                    **result,
                )
            )

    compile_run(out / "input.ll", "O0", "O0")
    run(
        [
            tools["opt"],
            "-S",
            "-passes=default<O2>",
            out / "input.ll",
            "-o",
            out / "optimized.ll",
        ],
        "O2-opt",
    )
    compile_run(out / "optimized.ll", "O2", "O2")
    bad = sources().replace(
        "%word = trunc i32 %index to i16",
        "%wrong = add i32 %index, 1\n%word = trunc i32 %wrong to i16",
    )
    (out / "wrong-index.ll").write_text(bad)
    compile_run(out / "wrong-index.ll", "wrong-index", "O2", 1)
    assert (
        "gpr(boundary, index_register)"
        in (out / "wrong-index-native.stderr").read_text()
    )
    bad = sources().replace(
        "ret i32 %index", "%narrow = and i32 %index, 65535\nret i32 %narrow"
    )
    (out / "wrong-return.ll").write_text(bad)
    compile_run(out / "wrong-return.ll", "wrong-return", "O2", 1)
    assert "returned.rax" in (out / "wrong-return-native.stderr").read_text()
    inputs = (
        Path(__file__),
        ROOT / "protected_bound_kvm.c",
        ROOT / "KvmProtectedFaultFixture.h",
        ROOT / "KvmTransferFixture.h",
    )
    summary = dict(
        profiles=profiles,
        tools={n: sha256(path) for n, path in tools.items()},
        inputs={path.name: sha256(path) for path in inputs},
        affinity=sorted(os.sched_getaffinity(0)),
        runner_sha256=sha256(runner),
        negative_controls=[
            "wrong input before intact BOUND",
            "truncated full-width return after intact BOUND",
        ],
    )
    (out / "qualification.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(
        json.dumps(
            dict(profiles=profiles, qualification=str(out / "qualification.json"))
        )
    )


if __name__ == "__main__":
    main()

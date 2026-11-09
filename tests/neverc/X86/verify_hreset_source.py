#!/usr/bin/env python3
"""Check actual NeverD HRESET exports using controlled instruction boundaries.

HRESET is decoded before replacement with a trap. This checks native compiler
input transport and returned values, not physical prediction-history effects.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exports", type=Path, required=True)
    parser.add_argument("--frontend", type=Path, required=True)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fallback", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tools = {"frontend": str(args.frontend.resolve()), "clang": shutil.which("clang")}
    tools.update(
        {
            name: str((args.llvm_bin / name).resolve())
            for name in ("llc", "llvm-objdump")
        }
    )
    assert all(tools.values())
    records, profiles, exports, negative = [], [], {}, set()

    def run(command, name, expected=0):
        command = list(map(str, command))
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=600,
            env=dict(os.environ, TMPDIR=str(out)),
        )
        (out / (name + ".stdout")).write_text(result.stdout)
        (out / (name + ".stderr")).write_text(result.stderr)
        records.append(dict(command=command, exit_code=result.returncode, log=name))
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    drivers = {}
    for bits in (32, 64):
        drivers[bits] = out / f"driver{bits}.o"
        run(
            [
                tools["clang"],
                f"-m{bits}",
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-c",
                ROOT / "hreset_source_control.c",
                "-o",
                drivers[bits],
            ],
            f"driver{bits}",
        )
    files = sorted(args.exports.glob("hreset_*.c"))
    objects = sorted(
        p
        for p in args.exports.glob("hreset_*.o")
        if p.read_bytes().startswith(b"\x7fELF")
    )
    assert len(files) == 48 and len(objects) == 8, (len(files), len(objects))
    for path in files + objects:
        match = re.fullmatch(
            r"hreset_(32|64)_\d+_[01]_\d+\.(high\.c|llvm\.c|o)", path.name
        )
        assert match, path
        bits = int(match[1])
        object_input = path.suffix == ".o"
        if bits != 64 and "neverc" in tools["frontend"] and not object_input:
            continue  # Legacy source belongs to the full LLVM/Clang frontend.
        exports[path.name] = sha256(path)
        for opt in ("O0", "O2"):
            checks = [False]
            if (
                not args.fallback
                and not object_input
                and opt == "O2"
                and bits not in negative
            ):
                checks.append(True)
            for wrong in checks:
                name = (
                    path.name.replace(".", "_")
                    + "-"
                    + opt
                    + ("-wrong-control" if wrong else "")
                )
                source, ir, obj, exe = (
                    out / (name + suffix) for suffix in (".c", ".ll", ".o", ".exe")
                )
                text = (
                    "extern unsigned virtualization_test(unsigned);\n"
                    if object_input
                    else path.read_text()
                )
                if wrong:
                    text = (
                        "#define __builtin_ia32_hreset(x) __builtin_ia32_hreset((x) ^ 0x80000000u)\n"
                        + text
                    )
                source.write_text(
                    text
                    + "\nunsigned invoke_hreset(unsigned x) { return virtualization_test(x); }\n"
                )
                triple = ("i386" if bits == 32 else "x86_64") + "-unknown-linux-gnu"
                run(
                    (
                        [
                            tools["frontend"],
                            "--target=" + triple,
                            "-" + opt,
                            "-ffreestanding",
                            "-fno-builtin-mimalloc",
                            "-fno-lto",
                            "-S",
                            "-emit-llvm",
                            source,
                            "-o",
                            ir,
                        ]
                        if "neverc" in tools["frontend"]
                        else [
                            tools["frontend"],
                            "--target=" + triple,
                            "-" + opt,
                            "-ffreestanding",
                            "-fno-lto",
                            "-S",
                            "-emit-llvm",
                            source,
                            "-o",
                            ir,
                        ]
                    ),
                    name + "-frontend",
                )
                if not object_input:
                    native = "call void @llvm.x86.hreset(i32" in ir.read_text()
                    assembly = " asm " in ir.read_text()
                    assert (native, assembly) == (
                        (False, True) if args.fallback else (True, False)
                    ), name
                run(
                    [
                        tools["llc"],
                        "-mtriple=" + triple,
                        "-" + opt,
                        "-relocation-model=pic",
                        "-verify-machineinstrs",
                        "-filetype=obj",
                        ir,
                        "-o",
                        obj,
                    ],
                    name + "-object",
                )
                candidate = path if object_input else obj
                assembly = run(
                    [tools["llvm-objdump"], "-d", candidate], name + "-assembly"
                )
                count = len(re.findall(r"\bhreset\s+\$0x0\b", assembly))
                binary = candidate.read_bytes()
                opcode = bytes.fromhex("f3 0f 3a f0 c0 00")
                assert count and binary.count(opcode) == count, assembly
                trapped = out / (name + "-controlled.o")
                trapped.write_bytes(
                    binary.replace(opcode, bytes.fromhex("0f 0b 90 90 90 90"))
                )
                run(
                    [
                        tools["clang"],
                        f"-m{bits}",
                        "-no-pie",
                        drivers[bits],
                        *([obj] if object_input else []),
                        trapped,
                        "-o",
                        exe,
                    ],
                    name + "-link",
                )
                result = run([exe], name + "-execute", expected=1 if wrong else 0)
                if wrong:
                    assert "control=1" in (out / (name + "-execute.stderr")).read_text()
                    negative.add(bits)
                    continue
                result = json.loads(result)
                assert result == dict(controlled_inputs_and_returns=4096), result
                profiles.append(
                    dict(
                        source=path.name,
                        optimization=opt,
                        original_object_sha256=sha256(candidate),
                        **result,
                    )
                )
        print(path.name, "PASS", flush=True)
    assert args.fallback or negative == {32, 64}
    report = dict(
        schema_version=1,
        inputs={
            p.name: sha256(p)
            for p in (Path(__file__), ROOT / "hreset_source_control.c")
        },
        exports=exports,
        tools={name: sha256(path) for name, path in tools.items()},
        affinity=sorted(os.sched_getaffinity(0)),
        fallback=args.fallback,
        profiles=profiles,
        checks=sum(p["controlled_inputs_and_returns"] for p in profiles),
        wrong_control_rejected_bits=sorted(negative),
        boundary="Actual NeverD x86/x64 HighC/LLVMC exports from three formats recompiled for Linux, plus exported ELF objects. Independently decoded HRESET is replaced by a trap; signals observe EAX and resume to check complete source return. No physical HRESET success or original fault/event semantics are inferred.",
    )
    (out / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

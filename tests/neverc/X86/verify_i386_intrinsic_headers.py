#!/usr/bin/env python3
"""Execute NeverC x86 intrinsic headers through full LLVM, including i386 PIC.

Requires Linux ELF32/ELF64 runtime support and SSE4.2/AVX2/ADX on the selected
CPU. HRESET and AVX-512 are compile-only checks, never executed by the fixture.
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
    parser.add_argument("--neverc", type=Path, required=True)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--machine-backend", action="store_true",
                        help="use NeverC -c for the i386 objects")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tools = {"neverc": str(args.neverc.resolve()), "clang": shutil.which("clang")}
    tools.update(
        {
            name: str((args.llvm_bin / name).resolve())
            for name in ("llc", "opt", "llvm-objdump")
        }
    )
    assert all(tools.values())
    records, profiles, controls = [], [], []

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

    def compile_case(bits, opt, pic, wrong=False):
        name = f"x{bits}-{opt}-{'pic' if pic else 'static'}" + (
            "-wrong-add" if wrong else ""
        )
        triple = ("i386" if bits == 32 else "x86_64") + "-unknown-linux-gnu"
        ir, obj, exe = (out / (name + suffix) for suffix in (".ll", ".o", ".exe"))
        frontend = [
                tools["neverc"],
                "--target=" + triple,
                "-" + opt,
                "-std=c23",
                "-mavx2",
                "-msse4.2",
                "-madx",
                "-Werror",
                "-ffreestanding",
                "-fno-builtin-mimalloc",
                "-fno-lto",
                "-fPIC" if pic else "-fno-pic",
                *(["-DWRONG_ADD"] if wrong else []),
                "-S",
                "-emit-llvm",
                ROOT / "i386_intrinsic_headers.c",
                "-o",
                ir,
            ]
        run(frontend, name + "-frontend")
        text = ir.read_text()
        history = re.search(r"^define[^\n]*@history\([^\n]*\).*?^}", text, re.M | re.S)
        assert history and "call void @llvm.x86.hreset(" in history[0]
        assert " asm " not in history[0]
        assert "@llvm.x86.mmx.padd.w(" in text and "@llvm.x86.mmx.emms(" in text
        run([tools["opt"], "-passes=verify", "-disable-output", ir], name + "-verify")
        run(
            [
                tools["llc"],
                "-mtriple=" + triple,
                "-" + opt,
                "-relocation-model=" + ("pic" if pic else "static"),
                "-verify-machineinstrs",
                "-filetype=obj",
                ir,
                "-o",
                obj,
            ],
            name + "-object",
        )
        if args.machine_backend and bits == 32:
            native = [x for x in frontend if x not in ("-S", "-emit-llvm")]
            native[-1] = obj
            run(native + ["-c", "-fllvm-backend=" + tools["llc"]], name + "-native")
        assembly = run([tools["llvm-objdump"], "-d", obj], name + "-assembly")
        assert re.search(r"\bhreset\b", assembly) and "%zmm" in assembly
        run(
            [tools["clang"], f"-m{bits}", "-pie" if pic else "-no-pie", obj, "-o", exe],
            name + "-link",
        )
        result = run([exe], name + "-execute", expected=1 if wrong else 0)
        if wrong:
            assert (
                "out[lane] == a[lane] + b[lane]"
                in (out / (name + "-execute.stderr")).read_text()
            )
            return name
        result = json.loads(result)
        assert result == dict(cases=4096, checks=110592), result
        return dict(
            name=name,
            bits=bits,
            optimization=opt,
            pic=pic,
            ir_sha256=sha256(ir),
            object_sha256=sha256(obj),
            executable_sha256=sha256(exe),
            **result,
        )

    for bits in (32, 64):
        for opt in ("O0", "O2"):
            for pic in (False, True):
                profile = compile_case(bits, opt, pic)
                profiles.append(profile)
                print(profile["name"], profile["checks"], flush=True)
        controls.append(compile_case(bits, "O2", True, wrong=True))
    report = dict(
        schema_version=1,
        machine_backend=args.machine_backend,
        inputs={
            path.name: sha256(path)
            for path in (Path(__file__), ROOT / "i386_intrinsic_headers.c")
        },
        tools={name: sha256(path) for name, path in tools.items()},
        affinity=sorted(os.sched_getaffinity(0)),
        profiles=profiles,
        checks=sum(profile["checks"] for profile in profiles),
        negative_controls=controls,
        boundary="Linux i386/x64 C23, O0/O2, static/PIC objects via full LLVM; MMX/SSE/AVX2 values, carry/borrow, CRC32C, x87 after EMMS and marker preservation of full-width EBX/RBX. HRESET and AVX-512 are compile-only. No original-machine state/fault/event claim.",
    )
    (out / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

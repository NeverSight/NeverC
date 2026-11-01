#!/usr/bin/env python3
"""Compare full-dividend LLVM/NeverC division with original i386/x64 instructions."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources(mode):
    ir, c, cases = [], [], []
    c.append("typedef unsigned long long u64;\n")
    for bits in (8, 16, 32, 64) if mode == 64 else (8, 16, 32):
        ty = f"i{bits}"
        cty = {8: "unsigned char", 16: "unsigned short", 32: "unsigned int", 64: "u64"}[bits]
        pair = "{ " + ty + ", " + ty + " }"
        for signed in (False, True):
            name = ("s" if signed else "u") + f"divrem{bits}"
            ir.append(f"declare {pair} @llvm.x86.{name}({ty}, {ty}, {ty})\n")
            c.append(f"#if !__has_builtin(__builtin_ia32_{name})\n#error missing native division\n#endif\n")
            for results in range(4):
                symbol = f"probe_{name}_{results}"
                cases.append(f"DIVISION_CASE({symbol}, {bits}, {int(signed)}, {results})\n")
                ir.append(f"define void @{symbol}(i64 %l, i64 %h, i64 %d, ptr %q, ptr %r, ptr %marker) {{\n"
                          "  store i8 17, ptr %marker\n")
                operands = ["%l", "%h", "%d"]
                if bits != 64:
                    for value in operands:
                        ir.append(f"  {value}n = trunc i64 {value} to {ty}\n")
                    operands = [x + "n" for x in operands]
                ir.append(f"  %pair = call {pair} @llvm.x86.{name}(" +
                          ", ".join(f"{ty} {value}" for value in operands) + ")\n")
                for index, dest in enumerate(("q", "r")):
                    if results & (1 << index):
                        ir.append(f"  %{dest}v = extractvalue {pair} %pair, {index}\n")
                        value = f"%{dest}v"
                        if bits != 64:
                            ir.append(f"  %{dest}w = zext {ty} {value} to i64\n")
                            value = f"%{dest}w"
                        ir.append(f"  store i64 {value}, ptr %{dest}\n")
                ir.append("  store i8 29, ptr %marker\n  ret void\n}\n")
                c.append(f"void {symbol}(u64 l,u64 h,u64 d,u64 *q,u64 *r,unsigned char *marker) {{\n"
                         f"  *marker=17; {cty} rem;\n"
                         f"  {cty} quotient=__builtin_ia32_{name}(({cty})l,({cty})h,({cty})d,&rem);\n")
                if results & 1:
                    c.append("  *q=quotient;\n")
                if results & 2:
                    c.append("  *r=rem;\n")
                c.append("  *marker=29;\n}\n")
    return "".join(ir), "".join(c), "".join(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--neverc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", type=int, choices=(32, 64), action="append")
    parser.add_argument("--backend", choices=("llvm-legacy", "llvm-new", "neverc", "neverc-ir-legacy", "neverc-ir-new"), action="append")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = {"clang": Path(shutil.which("clang") or ""), "neverc": args.neverc.resolve()}
    tools.update({name: (args.llvm_bin / name).resolve() for name in ("llc", "opt", "llvm-objdump")})
    for name, path in tools.items():
        if not path.is_file():
            parser.error(f"missing {name}: {path}")
    environment = dict(os.environ, TMPDIR=str(output))
    commands, profiles = [], []

    def run(command, name):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=240)
        (output / (name + ".stdout")).write_text(result.stdout)
        (output / (name + ".stderr")).write_text(result.stderr)
        commands.append(dict(command=command, exit_code=result.returncode))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    for mode in args.mode or (32, 64):
        directory = output / str(mode)
        directory.mkdir(exist_ok=True)
        ir, c, cases = sources(mode)
        source_ir, source_c = directory / "division.ll", directory / "division.c"
        source_ir.write_text(ir)
        source_c.write_text(c)
        (directory / "division_cases.inc").write_text(cases)
        oracle = directory / "oracle.o"
        run([tools["clang"], "-std=gnu11", "-O2", f"-m{mode}", "-Wall", "-Wextra", "-Werror",
             "-fno-pie", "-I", directory, "-c", ROOT / "native_division_harness.c", "-o", oracle], f"{mode}-oracle")
        backends = args.backend or ("llvm-legacy", "llvm-new", "neverc", "neverc-ir-legacy", "neverc-ir-new")
        for backend in backends:
            if mode == 32 and backend.startswith("neverc"):
                continue  # NeverC's frontend targets x64; this is a declared matrix.
            for level in ("O0", "O2"):
                name = f"{mode}-{backend}-{level}"
                obj, exe = (directory / (name + suffix) for suffix in (".o", ".exe"))
                flags = ["--target=x86_64-unknown-linux-gnu", "-std=c23", "-" + level, "-fno-lto",
                         "-ffreestanding", "-fno-builtin-mimalloc", "-fno-stack-protector", "-fno-pic", "-fno-pie",
                         "-mllvm", "-verify-machineinstrs"]
                lowered = source_ir
                if backend == "neverc":
                    run([tools["neverc"], *flags, "-c", source_c, "-o", obj], name + "-compile")
                else:
                    if backend.startswith("neverc-ir"):
                        lowered = directory / (name + ".ll")
                        run([tools["neverc"], *flags, "-S", "-emit-llvm", source_c, "-o", lowered], name + "-ir")
                        if "@llvm.x86.udivrem8(" not in lowered.read_text() or "asm " in lowered.read_text():
                            raise RuntimeError("missing native division IR")
                    if level == "O2":
                        optimized = directory / (name + "-optimized.ll")
                        run([tools["opt"], "-S", "-passes=default<O2>", lowered, "-o", optimized], name + "-opt")
                        lowered = optimized
                    run([tools["llc"], "-mtriple=" + ("i386" if mode == 32 else "x86_64") + "-unknown-linux-gnu",
                         "-" + level, "-filetype=obj", "-verify-machineinstrs",
                         *(["-enable-new-pm"] if backend.endswith("new") else []), lowered, "-o", obj], name + "-compile")
                assembly = run([tools["llvm-objdump"], "-d", obj], name + "-assembly")
                instructions = re.findall(r"\b(?:i?div)[bwlq]?\s", assembly)
                expected_functions = 24 if mode == 32 else 32
                if len(instructions) != expected_functions:
                    raise RuntimeError(f"{name}: expected exactly one divide in each function")
                run([tools["clang"], f"-m{mode}", "-no-pie", oracle, obj, "-o", exe], name + "-link")
                observations = json.loads(run([exe], name + "-run"))
                if observations["functions"] != expected_functions or observations["comparisons"] != expected_functions * 12**3:
                    raise RuntimeError(f"{name}: incomplete native matrix")
                profiles.append(dict(name=name, mode=mode, backend=backend, optimization=level,
                                     object_sha256=digest(obj), source_sha256=digest(source_c if backend.startswith("neverc") else source_ir),
                                     **observations))
                print(f"PASS {name}: {observations}", flush=True)
    report = dict(schema_version=1, tools={name: digest(path) for name, path in tools.items()
                                   if name != "neverc" or any(p["backend"].startswith("neverc") for p in profiles)},
                  oracle_sha256=digest(ROOT / "native_division_harness.c"), profiles=profiles,
                  comparisons=sum(p["comparisons"] for p in profiles), faults=sum(p["faults"] for p in profiles),
                  boundary="Linux user-mode native register DIV/IDIV; OS handles #DE as SIGFPE; no guest event delivery")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

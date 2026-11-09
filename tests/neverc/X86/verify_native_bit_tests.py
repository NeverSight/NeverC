#!/usr/bin/env python3
"""Compare native LLVM/NeverC bit-string APIs with original x64 instructions."""
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
    assert mode == 64
    ir, c, cases = [], [], []
    for bits in (16, 32, 64):
        ty = f"i{bits}"
        cty = {16: "short", 32: "int", 64: "long long"}[bits]
        for action, op in enumerate(("bt", "bts", "btr", "btc")):
            intrinsic = f"llvm.x86.{op}.mem{bits}"
            ir.append(f"declare i8 @{intrinsic}(ptr, {ty})\n")
            for used in (0, 1):
                symbol = f"probe_{op}{bits}_{used}"
                cases.append(f"BIT_CASE({symbol},{bits},{action},{used})\n")
                ir.append(f"define i8 @{symbol}(ptr %p,i64 %index,ptr %marker) {{\n"
                          "  store i8 17, ptr %marker\n")
                value = "%index"
                if bits != 64:
                    ir.append(f"  %i = trunc i64 %index to {ty}\n")
                    value = "%i"
                ir.append(f"  %cf = call i8 @{intrinsic}(ptr %p,{ty} {value})\n"
                          "  store i8 29, ptr %marker\n"
                          f"  ret i8 {'%cf' if used else '0'}\n}}\n")
                c.append(f"unsigned char {symbol}(void *p,long long index,unsigned char *marker) {{\n"
                         f" *marker=17; unsigned char cf=__builtin_ia32_{op}_mem{bits}(p,({cty})index);\n"
                         f" *marker=29; return {'cf' if used else '0'};\n}}\n")
    return "".join(ir), "".join(c), "".join(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--neverc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", type=int, choices=(64,), action="append")
    parser.add_argument("--backend", choices=("llvm-legacy", "llvm-new", "neverc", "neverc-ir-legacy", "neverc-ir-new"), action="append")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = {"clang": Path(shutil.which("clang") or ""), "neverc": args.neverc.resolve()}
    tools.update({name: (args.llvm_bin / name).resolve() for name in ("llc", "opt", "llvm-objdump")})
    for name, path in tools.items():
        if not path.is_file():
            parser.error(f"missing {name}: {path}")
    tool_hashes = {name: digest(path) for name, path in tools.items()}
    inputs = {str(p): digest(p) for p in (Path(__file__), ROOT / "native_bit_test_harness.c")}
    environment = dict(os.environ, TMPDIR=str(output))
    commands, profiles, negative_controls = [], [], []

    def run(command, name, expected=0):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=300)
        (output / (name + ".stdout")).write_text(result.stdout)
        (output / (name + ".stderr")).write_text(result.stderr)
        commands.append(dict(command=command, exit_code=result.returncode, expected_exit_code=expected))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    for mode in args.mode or (64,):
        directory = output / str(mode)
        directory.mkdir(exist_ok=True)
        ir, c, cases = sources(mode)
        source_ir, source_c = directory / "bits.ll", directory / "bits.c"
        source_ir.write_text(ir)
        source_c.write_text(c)
        (directory / "bit_cases.inc").write_text(cases)
        oracle = directory / "oracle.o"
        run([tools["clang"], "-std=gnu11", "-O2", f"-m{mode}", "-Wall", "-Wextra", "-Werror",
             "-fno-pie", "-I", directory, "-c", ROOT / "native_bit_test_harness.c", "-o", oracle], f"{mode}-oracle")
        backends = args.backend or ("llvm-legacy", "llvm-new", "neverc", "neverc-ir-legacy", "neverc-ir-new")
        for backend in backends:
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
                        if "@llvm.x86.bt.mem16(" not in lowered.read_text() or "asm " in lowered.read_text():
                            raise RuntimeError("missing native bit-string IR")
                    if level == "O2":
                        optimized = directory / (name + "-optimized.ll")
                        run([tools["opt"], "-S", "-passes=default<O2>", lowered, "-o", optimized], name + "-opt")
                        lowered = optimized
                    run([tools["llc"], "-mtriple=" + "x86_64" + "-unknown-linux-gnu",
                         "-" + level, "-filetype=obj", "-verify-machineinstrs",
                         *(["-enable-new-pm"] if backend.endswith("new") else []), lowered, "-o", obj], name + "-compile")
                assembly = run([tools["llvm-objdump"], "-d", obj], name + "-assembly")
                instructions = re.findall(r"\bbt[csr]?[wlq]?\s", assembly)
                expected_functions = 24
                if len(instructions) != expected_functions:
                    raise RuntimeError(f"{name}: expected exactly one bit operation per function")
                run([tools["clang"], f"-m{mode}", "-no-pie", oracle, obj, "-o", exe], name + "-link")
                observations = json.loads(run([exe], name + "-run"))
                expected = expected_functions * 4 * (8 * 515 + 8) + (8 * (2 + 4 + 8)) * 16
                if observations["functions"] != expected_functions or observations["comparisons"] != expected or not observations["faults"]:
                    raise RuntimeError(f"{name}: incomplete native matrix {observations}")
                profiles.append(dict(name=name, mode=mode, backend=backend, optimization=level,
                                     object_sha256=digest(obj), executable_sha256=digest(exe), **observations))
                print(f"PASS {name}: {observations}", flush=True)
        # Keep the instruction, addressing and CF but erase one BTS store.
        # Rejecting this control proves that matching carry alone is insufficient.
        bad = directory / "wrong-store.ll"
        bad.write_text(ir.replace("call i8 @llvm.x86.bts.mem16", "call i8 @llvm.x86.bt.mem16", 1))
        obj, exe = directory / "wrong-store.o", directory / "wrong-store.exe"
        run([tools["llc"], "-mtriple=" + "x86_64", "-O2",
             "-verify-machineinstrs", "-filetype=obj", bad, "-o", obj], f"{mode}-control-compile")
        run([tools["clang"], f"-m{mode}", "-no-pie", oracle, obj, "-o", exe], f"{mode}-control-link")
        run([exe], f"{mode}-control-run", expected=1)
        negative_controls.append(dict(mode=mode, rejected=True, executable_sha256=digest(exe)))
    if any(digest(tools[name]) != h for name, h in tool_hashes.items()) or any(digest(p) != h for p, h in inputs.items()):
        raise RuntimeError("qualification inputs changed during execution")
    report = dict(schema_version=1, tools=tool_hashes, inputs=inputs, profiles=profiles,
                  negative_controls=negative_controls,
                  comparisons=sum(p["comparisons"] for p in profiles), faults=sum(p["faults"] for p in profiles),
                  boundary="Linux user-mode memory bit strings: CF, complete fixture RAM, signed/extreme indices, unaligned accesses, retained dead effects, SIGSEGV/SIGBUS address/error and marker ordering; no guest event delivery or atomic concurrency claim")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Execute the full LLVM/Clang BOUND APIs against independent IA32 bytes.

NeverC's frontend remains x64/AArch64. BOUND has no long-mode encoding.
This witness qualifies the full fork, including live indices, dead results,
ordered memory, conditional faults, address16 wrapping and nonflat FS.
"""

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


def generate():
    ir, c, header, assembly, cases = [], [], [], [".text"], []
    for width in (2, 4):
        ty, cty = f"i{width*8}", "short" if width == 2 else "int"
        for bits, prefix, explicit in (
            (32, 0, False),
            (32, 0, True),
            (32, 0x64, True),
            (16, 0x64, True),
        ):
            native = f"llvm.x86.bound{width*8}" + (".address32" if explicit else "")
            builtin = f"__builtin_ia32_bound{width*8}" + (
                "_address32" if explicit else ""
            )
            signature = (
                f"{ty}, i32, i32 immarg, i32 immarg" if explicit else f"{ty}, ptr"
            )
            declaration = f"declare void @{native}({signature})\n"
            if declaration not in ir:
                ir.append(declaration)
            raw = f"raw_{width}_{bits}_{prefix}"
            if raw not in cases:
                cases.append(raw)
                data = (
                    ([0x66] if width == 2 else [])
                    + ([0x67] if bits == 16 else [])
                    + ([prefix] if prefix else [])
                    + [0x62, 7 if bits == 16 else 3]
                )
                assembly += [
                    f".globl {raw}\n{raw}:",
                    "pushl %ebx",
                    "movl 8(%esp), %eax",
                    "movl 12(%esp), %ebx",
                    "movl 16(%esp), %edx",
                    "movb $1, (%edx)",
                    ".byte " + ",".join(map(str, data)),
                    "movb $2, (%edx)",
                    "popl %ebx",
                    "ret",
                ]
                header.append(
                    f"extern unsigned {raw}(unsigned, unsigned, volatile unsigned char *);\n"
                )
            for variant in ("live", "dead", "twice"):
                name = f"probe_{width}_{bits}_{prefix}_{int(explicit)}_{variant}"
                header.append(
                    f"extern unsigned {name}(unsigned, unsigned, volatile unsigned char *);\n"
                )
                narrow = "%index" if width == 4 else "%word"
                body = [f"define i32 @{name}(i32 %index, i32 %offset, ptr %marker) {{"]
                if width == 2:
                    body.append("%word = trunc i32 %index to i16")
                body += [
                    "%pointer = inttoptr i32 %offset to ptr",
                    "store i8 1, ptr %marker",
                ]
                args = f"{ty} {narrow}, " + (
                    f"i32 %offset, i32 {bits}, i32 {prefix}"
                    if explicit
                    else "ptr %pointer"
                )
                body += [f"call void @{native}({args})"] * (
                    2 if variant == "twice" else 1
                )
                body += [
                    "store i8 2, ptr %marker",
                    "ret i32 " + ("42" if variant == "dead" else "%index"),
                    "}\n",
                ]
                ir.append("\n".join(body))
                carg = f"({cty})index, " + (
                    f"offset, {bits}, {prefix}" if explicit else "(void *)offset"
                )
                c += [
                    f"unsigned {name}(unsigned index, unsigned offset, unsigned char *marker) {{",
                    "*marker = 1;",
                ]
                c += [f"{builtin}({carg});"] * (2 if variant == "twice" else 1)
                c += [
                    "*marker = 2;",
                    "return " + ("42" if variant == "dead" else "index") + ";",
                    "}",
                ]
                cases.append((name, raw, width, bits, prefix, variant == "dead"))
    header.append("static const struct Case Cases[] = {\n")
    for case in cases:
        if isinstance(case, tuple):
            name, raw, width, bits, prefix, dead = case
            header.append(
                f'{{{name},{raw},{width},{bits},{prefix},{int(dead)},"{name}"}},\n'
            )
    header.append("};\n")
    assembly.append('.section .note.GNU-stack,"",@progbits\n')
    return "\n".join(ir), "\n".join(c), "".join(header), "\n".join(assembly)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--clang", type=Path)
    parser.add_argument(
        "--neverc",
        type=Path,
        help="compile i386 source to IR, then use the full LLVM backend",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--host-clang", default="clang")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    records, results = [], []

    def run(cmd, name, expected=0):
        cmd = list(map(str, cmd))
        start = time.monotonic()
        r = subprocess.run(cmd, text=True, capture_output=True)
        (out / (name + ".stdout")).write_text(r.stdout)
        (out / (name + ".stderr")).write_text(r.stderr)
        records.append(
            dict(
                command=cmd,
                exit_code=r.returncode,
                seconds=time.monotonic() - start,
                log=name,
            )
        )
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if (expected == 0 and r.returncode != 0) or (
            expected != 0 and r.returncode == 0
        ):
            raise RuntimeError(
                f"{name}: unexpected exit {r.returncode}\n{r.stdout}\n{r.stderr}"
            )
        return r.stdout

    for name, text in zip(
        ("input.ll", "input.c", "bound_cases.inc", "original.s"), generate()
    ):
        (out / name).write_text(text)
    compilers = ["llvm"] + (["clang"] if args.clang else [])
    compilers += ["neverc"] if args.neverc else []
    for compiler in compilers:
        for level in ("O0", "O2"):
            stem = compiler + "-" + level
            obj = out / (stem + ".o")
            if compiler == "llvm":
                source = out / "input.ll"
                if level == "O2":
                    source = out / "optimized.ll"
                    run(
                        [
                            args.llvm_bin / "opt",
                            "-S",
                            "-passes=default<O2>",
                            out / "input.ll",
                            "-o",
                            source,
                        ],
                        stem + "-opt",
                    )
                run(
                    [
                        args.llvm_bin / "llc",
                        "-mtriple=i386-unknown-linux-gnu",
                        "-" + level,
                        "-verify-machineinstrs",
                        "-filetype=obj",
                        source,
                        "-o",
                        obj,
                    ],
                    stem + "-compile",
                )
            elif compiler == "neverc":
                source = out / (stem + ".ll")
                run(
                    [
                        args.neverc,
                        "--target=i386-unknown-linux-gnu",
                        "-" + level,
                        "-fno-pie",
                        "-S",
                        "-emit-llvm",
                        out / "input.c",
                        "-o",
                        source,
                    ],
                    stem + "-frontend",
                )
                assert "asm " not in source.read_text()
                run(
                    [
                        args.llvm_bin / "opt",
                        "-passes=verify",
                        "-disable-output",
                        source,
                    ],
                    stem + "-verify",
                )
                run(
                    [
                        args.llvm_bin / "llc",
                        "-" + level,
                        "-verify-machineinstrs",
                        "-filetype=obj",
                        source,
                        "-o",
                        obj,
                    ],
                    stem + "-compile",
                )
            else:
                run(
                    [
                        args.clang,
                        "-m32",
                        "-" + level,
                        "-fno-pie",
                        "-c",
                        out / "input.c",
                        "-o",
                        obj,
                    ],
                    stem + "-compile",
                )
            disasm = run([args.llvm_bin / "llvm-objdump", "-d", obj], stem + "-disasm")
            assert (
                len(re.findall(r"\tbound\s", disasm)) == 32
            ), "a faulting native instruction was removed/merged"
            exe = out / stem
            run(
                [
                    args.host_clang,
                    "-m32",
                    "-O2",
                    "-fno-pie",
                    "-no-pie",
                    "-I",
                    out,
                    ROOT / "bound_completion.c",
                    out / "original.s",
                    obj,
                    "-o",
                    exe,
                ],
                stem + "-link",
            )
            results.append(
                dict(
                    compiler=compiler,
                    optimization=level,
                    **json.loads(run([exe], stem + "-cpu")),
                )
            )
    # Keep BOUND intact but transport a wrong index. The native witness must fail.
    bad = (
        (out / "input.ll")
        .read_text()
        .replace(
            "%word = trunc i32 %index to i16",
            "%bad = add i32 %index, 1\n%word = trunc i32 %bad to i16",
        )
    )
    (out / "wrong-index.ll").write_text(bad)
    run(
        [
            args.llvm_bin / "llc",
            "-mtriple=i386-unknown-linux-gnu",
            "-O2",
            "-filetype=obj",
            out / "wrong-index.ll",
            "-o",
            out / "wrong-index.o",
        ],
        "negative-compile",
    )
    run(
        [
            args.host_clang,
            "-m32",
            "-O2",
            "-fno-pie",
            "-no-pie",
            "-I",
            out,
            ROOT / "bound_completion.c",
            out / "original.s",
            out / "wrong-index.o",
            "-o",
            out / "wrong-index",
        ],
        "negative-link",
    )
    run([out / "wrong-index"], "negative-cpu", expected=1)
    summary = dict(
        profiles=results,
        negative_controls=["wrong index before intact BOUND rejected"],
        source_sha256={
            str(p): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (Path(__file__), ROOT / "bound_completion.c")
        },
        tools={
            str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [args.llvm_bin / n for n in ("llc", "opt", "llvm-objdump")]
            + ([args.clang] if args.clang else [])
            + ([args.neverc] if args.neverc else [])
            + [Path(shutil.which(args.host_clang) or args.host_clang)]
        },
        affinity=sorted(os.sched_getaffinity(0)),
        executables={
            str(p.name): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in out.iterdir()
            if p.is_file() and os.access(p, os.X_OK)
        },
    )
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(dict(profiles=results, summary=str(out / "summary.json"))))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare C record layout constants with Clang across platform data models.

This checks tree layout, including bitfield allocation and explicit alignment.
The platform ABI witness separately executes field access and mixed calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

TRIPLES = (
    "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
    "x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
    "x86_64-apple-macosx10.13", "aarch64-apple-macosx11.0",
)

BODIES = (
    "unsigned a:3; unsigned short b:9; unsigned c:13;",
    "unsigned a:31; unsigned b:2; unsigned c:17;",
    "char a; unsigned b:9; char c; unsigned d:7;",
    "unsigned :0; char a; unsigned :0; char b;",
    "short a:3; unsigned :0; short b:2;",
    "short a:3; unsigned :0; unsigned :0; short b:2;",
    "unsigned a:3; unsigned short :0; unsigned b:9;",
    "unsigned long long a:37; unsigned b:19;",
    "char a; double b; short c;",
    "char a; long double b; void *c;",
    "char a; struct Base b; char c;",
    "char a; struct Base b[2]; char c;",
    "char a; __declspec(align(16)) unsigned b; char c;",
    "char a; __declspec(align(16)) unsigned b:3; char c;",
    "unsigned a:3; __declspec(align(16)) unsigned b:7;",
    "char a; unsigned _BitInt(65) b[2]; char c;",
    "char a; union { unsigned x:5; unsigned short y:9; } b;",
    "char a; struct { unsigned x:5; unsigned short y:9; } b;",
)


def source():
    declarations = ["struct __declspec(align(16)) Base { unsigned x; };\n"]
    records = []
    for kind in ("struct", "union"):
        for pack in (0, 1, 2, 4, 8, 16):
            for alignment in (0, 32):
                for body in BODIES:
                    name = "R" + str(len(records))
                    attr = f"__declspec(align({alignment}))" if alignment else ""
                    if pack:
                        declarations.append(f"#pragma pack(push, {pack})\n")
                    declarations.append(f"{kind} {attr} {name} {{{body} char tail;}};\n")
                    if pack:
                        declarations.append("#pragma pack(pop)\n")
                    records.append(dict(name=name, kind=kind, pack=pack,
                                        alignment=alignment, body=body))
    constants = []
    for record in records:
        ty = record["kind"] + " " + record["name"]
        constants.extend((f"sizeof({ty})", f"_Alignof({ty})",
                          f"__builtin_offsetof({ty},tail)"))
    declarations.append("const unsigned record_layouts[]={" + ",".join(constants) + "};\n")
    return "".join(declarations), records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("neverc", "clang", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    text, records = source()
    src = args.output / "record-layout.c"
    src.write_text(text)
    commands, profiles = [], []
    for triple in TRIPLES:
        values = {}
        for name in ("clang", "neverc"):
            stem = triple + "-" + name
            ir = args.output / (stem + ".ll")
            cmd = [str(getattr(args, name)), "--target=" + triple, "-std=gnu23",
                   "-fms-extensions", "-ffreestanding", "-S", "-emit-llvm",
                   str(src), "-o", str(ir)]
            run = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            (args.output / (stem + ".log")).write_text(run.stdout + run.stderr)
            commands.append(dict(command=cmd, exit_code=run.returncode))
            assert run.returncode == 0, (stem, run.stderr)
            line = re.search(r"^@record_layouts =.*$", ir.read_text(), re.M)
            assert line, stem
            values[name] = [int(v) for v in re.findall(r"i32 (\d+)", line[0])]
            assert len(values[name]) == 3 * len(records), stem
        for i, record in enumerate(records):
            expected = values["clang"][i*3:i*3+3]
            actual = values["neverc"][i*3:i*3+3]
            assert actual == expected, (triple, record, expected, actual)
        profiles.append(dict(triple=triple, records=len(records),
                             comparisons=len(values["clang"])))
    report = dict(profiles=profiles, commands=commands, records=records,
                  tools={str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in (args.neverc, args.clang)},
                  artifacts={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in args.output.iterdir() if p.is_file()})
    (args.output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(dict(profiles=len(profiles),
                         comparisons=sum(p["comparisons"] for p in profiles))))


if __name__ == "__main__":
    main()

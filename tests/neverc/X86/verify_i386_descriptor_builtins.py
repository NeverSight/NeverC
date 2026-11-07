#!/usr/bin/env python3
"""Verify shared descriptor/machine builtins and native i386 query execution.

NeverC uses its explicit full-LLVM machine backend for i386. Privileged state
operations are code-generation checks; the four descriptor queries additionally
execute in native Linux i386/x64 processes against independent raw instructions.
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


def forms():
    for op in ("verr", "verw", "lldt", "ltr", "lmsw"):
        result = "unsigned char" if op.startswith("v") else "void"
        for source, ty in (("reg", "unsigned short"), ("mem", "const void *")):
            yield f"{op}_{source}", result, f"{ty} input", "input", op
    for op in ("sldt", "str", "smsw"):
        for width, ty in ((16, "unsigned short"), (32, "unsigned")):
            yield f"{op}_reg{width}", ty, "void", "", op
        yield f"{op}_mem", "void", "void *output", "output", op
    for op in ("lar", "lsl"):
        for width, ty in ((16, "unsigned short"), (32, "unsigned")):
            for source, first in (("reg", "unsigned short"), ("mem", "const void *")):
                yield (f"{op}_{source}{width}", ty,
                       f"{first} input, {ty} prior, unsigned char *status",
                       "input, prior, status", op)
    yield "invd", "void", "void", "", "invd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler, llvm = args.neverc.resolve(), args.llvm_bin.resolve()
    clang = shutil.which(args.clang)
    assert clang, "Clang with Linux multilib is required"
    env = dict(os.environ, TMPDIR=str(out))
    records, profiles = [], []

    def run(command, name, fail=False):
        command = list(map(str, command))
        p = subprocess.run(command, capture_output=True, env=env, timeout=300)
        (out / (name + ".stdout")).write_bytes(p.stdout)
        (out / (name + ".stderr")).write_bytes(p.stderr)
        records.append(dict(command=command, exit_code=p.returncode, log=name))
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        assert (p.returncode > 0 if fail else p.returncode == 0), (name, p.stderr.decode())
        return p.stdout.decode()

    entries = list(forms())
    assert len(entries) == 28
    source = out / "builtins.c"
    text = []
    for name, result, parameters, inputs, _ in entries:
        builtin = "__builtin_ia32_" + name
        text.append(f'_Static_assert(__has_builtin({builtin}), "{name}");')
        call = f"{builtin}({inputs})"
        text.append(f"{result} probe_{name}({parameters}) {{ {call}; "
                    f"{'return ' if result != 'void' else ''}{call}; }}")
    source.write_text("\n".join(text) + "\n")
    negative = out / "long-mode-only.c"
    wide = [f"{op}_reg64" for op in ("sldt", "str", "smsw")]
    wide += [f"{op}_{kind}64" for op in ("lar", "lsl") for kind in ("reg", "mem")]
    negative.write_text("\n".join(
        f'_Static_assert(!__has_builtin(__builtin_ia32_{name}), "{name}");'
        for name in wide) + "\n")
    for mode in (32, 64):
        target = "i386" if mode == 32 else "x86_64"
        base = [compiler, f"--target={target}-unknown-linux-gnu", "-Werror",
                "-fno-builtin-mimalloc", "-fno-lto", "-ffreestanding", "-fno-pie"]
        if mode == 32:
            base += ["-fllvm-backend=" + str(llvm / "llc")]
            run(base + ["-fsyntax-only", negative], "reject-wide")
        for level in ("O0", "O2"):
            tag = f"{target}-{level}"
            obj, ir = out / (tag + ".o"), out / (tag + ".ll")
            run(base + ["-" + level, "-S", "-emit-llvm", source, "-o", ir], tag + "-ir")
            ir_text = ir.read_text()
            assert not re.search(r"\basm\b", ir_text), tag
            run([llvm / "opt", "-passes=verify", "-disable-output", ir], tag + "-verify")
            run(base + ["-" + level, "-c", source, "-o", obj], tag + "-object")
            assembly = run([llvm / "llvm-objdump", "-d", obj], tag + "-disassembly")
            assert not re.search(r"\bcall[lq]?\b", assembly), assembly
            bodies = dict(re.findall(r"<[.]?(probe_\w+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
                                     assembly, re.S))
            for name, _, _, _, opcode in entries:
                body = bodies["probe_" + name]
                count = len(re.findall(r"\b" + opcode + r"[wlq]?\s", body))
                if opcode == "invd":
                    count = len(re.findall(r"\binvd\b", body))
                assert count == 2, (tag, name, body)
            query_obj = out / (tag + "-queries.o")
            run(base + ["-" + level, "-c", ROOT / "i386_descriptor_queries.c",
                        "-o", query_obj], tag + "-queries")
            exe = out / (tag + "-native")
            run([clang, f"-m{mode}", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-no-pie", ROOT / "i386_descriptor_query_oracle.c", query_obj,
                 "-o", exe], tag + "-link")
            actual = json.loads(run([exe], tag + "-native"))
            assert actual == {"native_query_comparisons": 1320, "native_assertions": 2200}, actual
            profiles.append(dict(profile=tag, native=actual, codegen_forms=len(entries),
                                 object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest()))
    report = dict(profiles=profiles, wide_only_forms=wide, commands=records,
                  compiler_sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),
                  full_llc_sha256=hashlib.sha256((llvm / "llc").read_bytes()).hexdigest())
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"profiles": len(profiles), "codegen_forms": len(entries),
                      "native_query_comparisons": sum(p["native"]["native_query_comparisons"]
                                                        for p in profiles)}, indent=2))


if __name__ == "__main__":
    main()

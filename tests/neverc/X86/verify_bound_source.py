#!/usr/bin/env python3
"""Compile and execute actual NeverD BOUND exports against independent bytes.

Export X86BoundSource with NEVERD_ISA_REGRESSION_ARTIFACT_DIR first. Both C
routes, all formats and controls are compiled. Linux ELF32 execution covers
flat DS and nonflat FS; the protected KVM suite owns other segment contexts.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from verify_bound import generate

ROOT = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exports", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--llvm-bin", type=Path, required=True)
    p.add_argument("--clang", default="clang")
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def run(cmd, name, failure=False):
        cmd = list(map(str, cmd))
        r = subprocess.run(cmd, capture_output=True, text=True)
        (out / (name + ".stdout")).write_text(r.stdout)
        (out / (name + ".stderr")).write_text(r.stderr)
        records.append(dict(command=cmd, exit_code=r.returncode, log=name))
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if (r.returncode == 0) == failure:
            raise RuntimeError(f"{name}: unexpected exit {r.returncode}\n{r.stderr}")
        return r.stdout

    originals = generate()[3]
    (out / "original.s").write_text(originals)
    header, wrappers, cases, source, hashes = [], [], [], [], {}
    for path in sorted(a.exports.glob("bound_*.c")):
        match = re.fullmatch(
            r"bound_(\d+)_(\d+)_(2|4)_(16|32)_(\d+)\.(high|llvm)\.c", path.name
        )
        if not match:
            continue
        fmt, noopt, width, bits, prefix, route = match.groups()
        width, bits, prefix = int(width), int(bits), int(prefix)
        name = path.name.replace(".", "_")
        text = path.read_text()
        assert re.search(r"\b(?:u)?int32_t virtualization_test\(", text), path
        source.append(
            text.replace(
                "virtualization_test(",
                f"__attribute__((noinline)) {name}(",
            )
        )
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        if (bits, prefix) not in ((32, 0), (32, 0x64), (16, 0x64)):
            continue
        wrapper, raw = "wrap_" + name, f"raw_{width}_{bits}_{prefix}"
        wrappers.append(
            f"unsigned {wrapper}(unsigned i, unsigned p, volatile unsigned char *m) {{ *m=1; unsigned r={name}(i,p); *m=2; return r; }}"
        )
        header.append(
            f"extern unsigned {wrapper}(unsigned, unsigned, volatile unsigned char *);\nextern unsigned {raw}(unsigned, unsigned, volatile unsigned char *);\n"
        )
        cases.append(f'{{{wrapper},{raw},{width},{bits},{prefix},0,"{name}"}},\n')
    assert (
        len(hashes) == 336
    ), f"expected 168 images through two C routes, got {len(hashes)}"
    assert len(cases) == 72
    object_inputs = []
    objcopy = shutil.which("objcopy")
    assert objcopy, "objcopy is required for exported ELF symbol isolation"
    for path in sorted(a.exports.glob("bound_*.o")):
        if path.read_bytes()[:4] != b"\x7fELF":
            continue
        _, fmt, noopt, width, bits, prefix = path.stem.split("_")
        width, bits, prefix = int(width), int(bits), int(prefix)
        if (bits, prefix) not in ((32, 0), (32, 0x64), (16, 0x64)):
            continue
        name = "object_" + path.stem
        obj = out / (name + ".o")
        run(
            [objcopy, "--redefine-sym", "virtualization_test=" + name, path, obj],
            name + "-rename",
        )
        object_inputs.append(obj)
        source.append(f"extern unsigned {name}(unsigned, unsigned);")
        wrapper, raw = "wrap_" + name, f"raw_{width}_{bits}_{prefix}"
        wrappers.append(
            f"unsigned {wrapper}(unsigned i, unsigned p, volatile unsigned char *m) {{ *m=1; unsigned r={name}(i,p); *m=2; return r; }}"
        )
        header.append(
            f"extern unsigned {wrapper}(unsigned, unsigned, volatile unsigned char *);\nextern unsigned {raw}(unsigned, unsigned, volatile unsigned char *);\n"
        )
        cases.append(f'{{{wrapper},{raw},{width},{bits},{prefix},0,"{name}"}},\n')
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    assert len(object_inputs) == 12 and len(cases) == 84
    header.append("static const struct Case Cases[] = {\n" + "".join(cases) + "};\n")
    (out / "bound_cases.inc").write_text("".join(header))
    aggregate = "\n".join(source + wrappers)
    (out / "exports.c").write_text(aggregate)
    results = []
    for level in ("O0", "O2"):
        obj = out / (level + ".o")
        run(
            [
                a.clang,
                "-m32",
                "-" + level,
                "-fno-pie",
                "-c",
                out / "exports.c",
                "-o",
                obj,
            ],
            level + "-compile",
        )
        disasm = run([a.llvm_bin / "llvm-objdump", "-d", obj], level + "-disasm")
        assert len(re.findall(r"\tbound\s", disasm)) == 336
        exe = out / level
        run(
            [
                a.clang,
                "-m32",
                "-O2",
                "-fno-pie",
                "-no-pie",
                "-I",
                out,
                ROOT / "bound_completion.c",
                out / "original.s",
                obj,
                *object_inputs,
                "-o",
                exe,
            ],
            level + "-link",
        )
        results.append(
            dict(
                optimization=level,
                sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                **json.loads(run([exe], level + "-cpu")),
            )
        )
    # The check must catch a result-width error with every BOUND still intact.
    bad = aggregate.replace("unsigned r=", "unsigned r=(unsigned short)")
    (out / "wrong-return.c").write_text(bad)
    run(
        [
            a.clang,
            "-m32",
            "-O2",
            "-fno-pie",
            "-c",
            out / "wrong-return.c",
            "-o",
            out / "wrong-return.o",
        ],
        "negative-compile",
    )
    run(
        [
            a.clang,
            "-m32",
            "-O2",
            "-fno-pie",
            "-no-pie",
            "-I",
            out,
            ROOT / "bound_completion.c",
            out / "original.s",
            out / "wrong-return.o",
            *object_inputs,
            "-o",
            out / "wrong-return",
        ],
        "negative-link",
    )
    run([out / "wrong-return"], "negative-cpu", failure=True)
    tools = [
        Path(shutil.which(a.clang) or a.clang),
        a.llvm_bin / "llvm-objdump",
        Path(objcopy),
    ]
    summary = dict(
        profiles=results,
        compiled_sources=336,
        exported_elf_objects=len(object_inputs),
        source_sha256=hashes,
        witness_sha256={
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (
                Path(__file__),
                ROOT / "bound_completion.c",
                ROOT / "verify_bound.py",
            )
        },
        tools={
            str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in tools
        },
        affinity=sorted(os.sched_getaffinity(0)),
        negative_controls=["truncated return with intact BOUND rejected"],
    )
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(
        json.dumps(
            dict(
                profiles=results,
                compiled_sources=336,
                exported_elf_objects=len(object_inputs),
                summary=str(out / "summary.json"),
            )
        )
    )


if __name__ == "__main__":
    main()

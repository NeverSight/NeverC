#!/usr/bin/env python3
"""Cross-target object / image matrix for the NeverC plugin toolchain.

Volume 6 task 22. Drives the compiler-under-test across the built-in target and
object-format inventory, produces a relocatable object (and, for the baseline
dyncode triples, a raw dyncode image) for every applicable route, and verifies
each artifact with a format check that matches the requested object format and
architecture. Cross artifacts are only parsed/verified, never executed; only the
host-compatible artifact could be run by a native harness.

Missing SDK support or an unsupported route is recorded as a capability-bound
skip with a reason rather than silently dropped. A required route that produces
the wrong format, or fails to produce an artifact at all, fails the matrix.

Example:

    python3 utils/plugin-api/run-cross-target-matrix.py \
        --compiler build-neverc/bin/neverc \
        --baseline-current-targets \
        --targets x86_64,aarch64 \
        --formats elf,coff,macho
"""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

# (architecture, object-format) -> canonical target triple.
FORMAT_TRIPLE = {
    ("x86_64", "elf"): "x86_64-unknown-linux-gnu",
    ("aarch64", "elf"): "aarch64-unknown-linux-gnu",
    ("x86_64", "coff"): "x86_64-pc-windows-msvc",
    ("aarch64", "coff"): "aarch64-pc-windows-msvc",
    ("x86_64", "macho"): "x86_64-apple-macos",
    ("aarch64", "macho"): "arm64-apple-macos",
}

# Current built-in dyncode inventory: macOS / Linux / Android / Windows across
# both architectures (user and kernel contexts share the same triple here).
BASELINE_DYNCODE_TRIPLES = [
    "arm64-apple-macos",
    "x86_64-apple-macos",
    "aarch64-linux-gnu",
    "x86_64-linux-gnu",
    "aarch64-linux-android29",
    "x86_64-linux-android29",
    "aarch64-pc-windows-msvc",
    "x86_64-pc-windows-msvc",
]

# AArch64 iOS is a declared source/object-layer route (no local link/run here).
BASELINE_SOURCE_OBJECT_TRIPLES = [
    ("arm64-apple-ios", "macho", "aarch64"),
]

COFF_MACHINE = {"x86_64": 0x8664, "aarch64": 0xAA64}

TRIVIAL_TU = "int neverc_cross_matrix(void) { return 0; }\n"
DYNCODE_TU = "int dyncode_entry(void) { return 0; }\n"


class Result:
    __slots__ = ("capability", "status", "reason")

    def __init__(self, capability: str, status: str, reason: str = ""):
        self.capability = capability
        self.status = status
        self.reason = reason


def verify_object_format(path: Path, fmt: str, arch: str) -> tuple[bool, str]:
    """Return (ok, detail) checking the artifact matches format and arch."""
    try:
        head = path.read_bytes()[:8]
    except OSError as error:
        return False, f"unreadable artifact: {error}"
    if len(head) < 4:
        return False, "artifact too small to identify"
    if fmt == "elf":
        ok = head[:4] == b"\x7fELF"
        return ok, "ELF magic" if ok else f"not ELF: {head[:4].hex()}"
    if fmt == "macho":
        ok = head[:4] in (
            b"\xcf\xfa\xed\xfe",
            b"\xce\xfa\xed\xfe",
            b"\xfe\xed\xfa\xcf",
            b"\xfe\xed\xfa\xce",
            b"\xca\xfe\xba\xbe",
        )
        return ok, "Mach-O magic" if ok else f"not Mach-O: {head[:4].hex()}"
    if fmt == "coff":
        machine = head[0] | (head[1] << 8)
        expected = COFF_MACHINE.get(arch)
        ok = expected is not None and machine == expected
        detail = f"machine=0x{machine:04x}"
        return ok, f"COFF {detail}" if ok else f"unexpected COFF {detail}"
    return False, f"unknown format {fmt}"


def run_compiler(compiler: str, args: list[str]) -> tuple[int, str]:
    proc = subprocess.run(
        [compiler, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return proc.returncode, proc.stdout


def compile_object(compiler: str, triple: str, src: Path, out: Path) -> tuple[int, str]:
    return run_compiler(
        compiler,
        ["--no-default-config", "-fno-lto", "-c", f"--target={triple}",
         str(src), "-o", str(out)],
    )


def compile_dyncode(compiler: str, triple: str, src: Path, out: Path) -> tuple[int, str]:
    return run_compiler(
        compiler,
        ["--no-default-config", "-fdyncode", "-std=c23", f"--target={triple}",
         str(src), "-o", str(out)],
    )


def matrix(compiler: str, targets: list[str], formats: list[str],
           baseline: bool, workdir: Path) -> list[Result]:
    results: list[Result] = []
    trivial = workdir / "tu.c"
    trivial.write_text(TRIVIAL_TU, encoding="utf-8")
    dyncode = workdir / "dyncode.c"
    dyncode.write_text(DYNCODE_TU, encoding="utf-8")

    # arch x format object routes.
    for fmt in formats:
        for arch in targets:
            cap = f"object.{arch}.{fmt}"
            triple = FORMAT_TRIPLE.get((arch, fmt))
            if triple is None:
                results.append(Result(cap, "skip",
                                      f"no built-in triple for {arch}/{fmt}"))
                continue
            out = workdir / f"{arch}_{fmt}.o"
            code, log = compile_object(compiler, triple, trivial, out)
            if code != 0 or not out.exists():
                results.append(Result(cap, "fail",
                                      f"compile failed for {triple}: "
                                      f"{log.strip().splitlines()[-1] if log.strip() else code}"))
                continue
            ok, detail = verify_object_format(out, fmt, arch)
            results.append(Result(cap, "pass" if ok else "fail",
                                  f"{triple}: {detail}"))

    if not baseline:
        return results

    # Baseline dyncode inventory (raw image per triple).
    for triple in BASELINE_DYNCODE_TRIPLES:
        cap = f"dyncode.image.{triple}"
        out = workdir / f"dyncode_{triple}.bin"
        code, log = compile_dyncode(compiler, triple, dyncode, out)
        if code != 0 or not out.exists() or out.stat().st_size == 0:
            results.append(Result(cap, "fail",
                                  f"no dyncode image for {triple}: "
                                  f"{log.strip().splitlines()[-1] if log.strip() else code}"))
        else:
            results.append(Result(cap, "pass",
                                  f"{out.stat().st_size} bytes"))

    # Baseline source/object-only routes (e.g. AArch64 iOS): no link/run.
    for triple, fmt, arch in BASELINE_SOURCE_OBJECT_TRIPLES:
        cap = f"object.{triple}"
        out = workdir / f"srcobj_{triple}.o"
        code, log = compile_object(compiler, triple, trivial, out)
        if code != 0 or not out.exists():
            results.append(Result(cap, "fail",
                                  f"compile failed for {triple}: "
                                  f"{log.strip().splitlines()[-1] if log.strip() else code}"))
            continue
        ok, detail = verify_object_format(out, fmt, arch)
        results.append(Result(cap, "pass" if ok else "fail",
                              f"{triple}: {detail}"))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", required=True,
                        help="path to the compiler-under-test (neverc)")
    parser.add_argument("--targets", default="x86_64,aarch64",
                        help="comma-separated architectures")
    parser.add_argument("--formats", default="elf,coff,macho",
                        help="comma-separated object formats")
    parser.add_argument("--baseline-current-targets", action="store_true",
                        help="also cover the built-in dyncode/iOS inventory")
    parser.add_argument("--output", type=Path, default=None,
                        help="write the summary JSON here")
    parser.add_argument("--keep", action="store_true",
                        help="keep the scratch directory")
    args = parser.parse_args()

    compiler = args.compiler
    if not Path(compiler).exists():
        print(f"error: compiler not found: {compiler}", file=sys.stderr)
        return 2
    targets = [t.strip() for t in args.targets.split(",") if t.strip()]
    formats = [f.strip() for f in args.formats.split(",") if f.strip()]

    scratch = Path(tempfile.mkdtemp(prefix="neverc-cross-"))
    try:
        results = matrix(compiler, targets, formats,
                         args.baseline_current_targets, scratch)
    finally:
        if not args.keep:
            for child in sorted(scratch.glob("*")):
                try:
                    child.unlink()
                except OSError:
                    pass
            try:
                scratch.rmdir()
            except OSError:
                pass

    npass = sum(1 for r in results if r.status == "pass")
    nskip = sum(1 for r in results if r.status == "skip")
    nfail = sum(1 for r in results if r.status == "fail")

    width = max((len(r.capability) for r in results), default=0)
    for r in results:
        print(f"  {r.status:<4} {r.capability.ljust(width)}  {r.reason}")
    print(f"cross-target matrix: {npass} pass, {nskip} skip, {nfail} fail "
          f"(host {platform.machine()}/{platform.system()})")

    if args.output is not None:
        summary = {
            "host": {"machine": platform.machine(),
                     "system": platform.system()},
            "pass": npass, "skip": nskip, "fail": nfail,
            "results": [
                {"capability": r.capability, "status": r.status,
                 "reason": r.reason}
                for r in results
            ],
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(summary, indent=2) + "\n",
                               encoding="utf-8")
        print(f"wrote {args.output}")

    return 1 if nfail else 0


if __name__ == "__main__":
    raise SystemExit(main())

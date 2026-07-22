#!/usr/bin/env python3

"""Validate the NeverC plugin single-header and the modular aggregate.

Checks, without a full build:

1. The generated distributed single header at ``pluginsdk/include`` is current
   (delegates to gen-single-header.py --check).
2. The generated SDK manifest is current (delegates to gen-sdk-manifest.py).
3. The source-tree modular aggregate ``neverc/include/.../NevercPluginAPI.h``
   includes the public modules in exactly the canonical manifest order.
4. The distributed single header is self-contained: no residual project or
   schema-fragment includes remain.
5. If a C compiler is available, the single header compiles as C and C++.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).resolve().parent / "plugin-api-modules.json"
AGGREGATE = ROOT / "neverc/include/neverc/Plugin/NevercPluginAPI.h"

AGG_INCLUDE = re.compile(r'^\s*#\s*include\s*"neverc/Plugin/(Plugin\w+\.h)"')
RESIDUAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"neverc/Plugin/')


def run_generator(script: str) -> int:
    result = subprocess.run(
        [sys.executable, str(Path(__file__).resolve().parent / script), "--check"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
    return result.returncode


def check_aggregate_order(document: dict) -> int:
    expected = [m["name"] for m in document["modules"] if m.get("single_header")]
    found = []
    for line in AGGREGATE.read_text(encoding="utf-8").splitlines():
        match = AGG_INCLUDE.match(line)
        if match:
            found.append(match.group(1))
    if found != expected:
        print(
            "modular aggregate NevercPluginAPI.h order does not match the "
            f"canonical manifest order.\n  expected: {expected}\n  found:    {found}",
            file=sys.stderr,
        )
        return 1
    return 0


def check_self_contained(document: dict) -> int:
    path = ROOT / document["single_header"]
    residual = [
        line
        for line in path.read_text(encoding="utf-8").splitlines()
        if RESIDUAL_INCLUDE.match(line)
    ]
    if residual:
        print(
            f"{path} is not self-contained; residual includes:\n  "
            + "\n  ".join(residual),
            file=sys.stderr,
        )
        return 1
    return 0


def compile_single_header(document: dict) -> int:
    include_dir = (ROOT / document["single_header"]).parents[2]
    header = '#include "neverc/Plugin/NevercPluginAPI.h"\n'
    body_c = header + "int probe(void){return (int)sizeof(NevercABITableHeader);}\n"
    body_cxx = header + 'extern "C" int probe(void);\nint probe(void){return (int)sizeof(NevercABITableHeader);}\n'
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    status = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        if cc:
            source = tmp_path / "probe.c"
            source.write_text(body_c, encoding="utf-8")
            result = subprocess.run(
                [cc, "-std=c11", "-I", str(include_dir), "-c", str(source),
                 "-o", str(tmp_path / "probe_c.o")],
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                sys.stderr.write(result.stderr)
                status = 1
        if cxx:
            source = tmp_path / "probe.cpp"
            source.write_text(body_cxx, encoding="utf-8")
            result = subprocess.run(
                [cxx, "-std=c++17", "-I", str(include_dir), "-c", str(source),
                 "-o", str(tmp_path / "probe_cxx.o")],
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                sys.stderr.write(result.stderr)
                status = 1
        if not cc and not cxx:
            print("no C/C++ compiler found; skipping compile check", file=sys.stderr)
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-compile", action="store_true", help="skip the compile check"
    )
    arguments = parser.parse_args()

    try:
        document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"cannot read module manifest: {error}", file=sys.stderr)
        return 1

    status = 0
    status |= run_generator("gen-single-header.py")
    status |= run_generator("gen-sdk-manifest.py")
    status |= check_aggregate_order(document)
    status |= check_self_contained(document)
    if not arguments.skip_compile:
        status |= compile_single_header(document)
    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())

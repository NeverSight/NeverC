#!/usr/bin/env python3
"""Record local P0 timing and dependency evidence; timings are not metadata."""

import argparse
import json
import platform
import statistics
import subprocess
import time
from pathlib import Path


def run(command):
    return subprocess.run([str(x) for x in command], text=True,
                          capture_output=True, check=True).stdout


def timed(command, count):
    samples = []
    for _ in range(count):
        start = time.perf_counter()
        run(command)
        samples.append(round((time.perf_counter() - start) * 1000, 3))
    return {"milliseconds": samples, "median_ms": statistics.median(samples)}


def tree_size(path):
    return sum(p.stat().st_size for p in path.rglob("*") if p.is_file())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=Path)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--llvm", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--runs", default=7, type=int)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent / "fixtures"
    resource = Path(run([args.llvm / "bin/clang", "-print-resource-dir"]).strip())
    result = {
        "host": platform.platform(),
        "frontend": run([args.llvm / "bin/clang++", "--version"]),
        "neverc": run([args.neverc, "--version"]),
        "files_bytes": {str(p): p.stat().st_size for p in (
            args.helper, args.llvm / "lib/libclang-cpp.dylib",
            args.llvm / "lib/libLLVM.dylib",
        )},
        "resource_headers_bytes": tree_size(resource / "include"),
        "startup_help": timed([args.helper, "--help"], args.runs),
        "translation": {},
        "dynamic_dependencies": {},
    }
    for name in ("identities.cpp", "sequencing.cpp"):
        result["translation"][name] = timed([
            args.helper, fixture / name, "--", "-std=c++17", "-nostdinc",
            "-nostdinc++", "-resource-dir", resource,
        ], args.runs)
    if platform.system() == "Darwin":
        for p in (args.helper, args.neverc, args.llvm / "lib/libclang-cpp.dylib",
                  args.llvm / "lib/libLLVM.dylib"):
            result["dynamic_dependencies"][str(p)] = run(["otool", "-L", p])
        undefined = run(["nm", "-u", args.helper])
        result["undefined_clang_or_llvm_symbols"] = sum(
            "_ZN5clang" in line or "_ZNK5clang" in line or "_ZN4llvm" in line
            or "_ZNK4llvm" in line for line in undefined.splitlines()
        )
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compile and run standard-library tests with a stock C toolchain.

The registry in ``tests/neverc/StdLibTests.cpp`` drives the real gate through
the ``neverc`` compiler, which takes hours to build.  Most standalone std tests
are plain C and only need the dependency list the registry already declares, so
this script reuses that list to get a minutes-scale signal from clang.

Tests that rely on compiler-specific behaviour (``-fbuiltin-std``, dot-method
syntax) cannot build here; name them explicitly only when they are portable.
"""

import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REGISTRY = REPO / "tests" / "neverc" / "StdLibTests.cpp"
TEST_DIR = REPO / "tests" / "neverc" / "std"


def load_registry():
    """Return {test_name: {source paths}} parsed from the C++ test registry."""
    spec = importlib.util.spec_from_file_location(
        "check_test_deps", REPO / "utils" / "lint" / "check-test-deps.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.parse_tests(REGISTRY.read_text())


def compile_command(cc, name, sources, sanitize, defines, out):
    args = [
        cc,
        "-std=gnu11",
        "-O1",
        "-g",
        "-fno-omit-frame-pointer",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-unused-function",
        f"-I{REPO}/std/include",
        f"-I{REPO}/std/src/net",
    ]
    if sanitize and sanitize != "none":
        args.append(f"-fsanitize={sanitize}")
    args.extend(f"-D{d}" for d in defines)
    args.append(str(TEST_DIR / f"test_{name}.c"))
    args.extend(str(REPO / "std" / source) for source in sorted(sources))
    args.extend(["-lm", "-pthread", "-lresolv", "-o", out])
    return args


def run_one(cc, name, sources, sanitize, defines, timeout, workdir):
    binary = os.path.join(workdir, f"test_{name}")
    build = subprocess.run(
        compile_command(cc, name, sources, sanitize, defines, binary),
        capture_output=True,
        text=True,
    )
    if build.returncode != 0:
        return "COMPILE-FAIL", build.stdout + build.stderr

    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "halt_on_error=1:detect_leaks=1")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    env.setdefault("TSAN_OPTIONS", "halt_on_error=1")
    try:
        run = subprocess.run(
            [binary],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", f"exceeded {timeout}s"

    output = run.stdout + run.stderr
    if run.returncode != 0:
        return f"EXIT-{run.returncode}", output
    if "passed" not in run.stdout:
        return "NO-PASS-MARKER", output
    return "OK", output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tests",
        default="",
        help="space-separated STD_TEST names (without the test_ prefix)",
    )
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument(
        "--sanitize",
        default="address,undefined",
        help="value for -fsanitize=, or 'none'",
    )
    parser.add_argument(
        "--define",
        action="append",
        default=[],
        help="extra -D macro, repeatable",
    )
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--list", action="store_true", help="print the registry and exit"
    )
    args = parser.parse_args()

    registry = load_registry()
    if args.list:
        for name in sorted(registry):
            print(name)
        return 0

    names = args.tests.split()
    if not names:
        print("error: --tests is required", file=sys.stderr)
        return 2

    unknown = [n for n in names if n not in registry]
    if unknown:
        print(f"error: not registered in StdLibTests.cpp: {unknown}",
              file=sys.stderr)
        return 2
    missing = [n for n in names if not (TEST_DIR / f"test_{n}.c").is_file()]
    if missing:
        print(f"error: missing test sources: {missing}", file=sys.stderr)
        return 2

    print(f"cc={args.cc} sanitize={args.sanitize} tests={len(names)}")
    failures = []
    with tempfile.TemporaryDirectory() as workdir:
        for name in names:
            status, output = run_one(
                args.cc,
                name,
                registry[name],
                args.sanitize,
                args.define,
                args.timeout,
                workdir,
            )
            print(f"::group::{status} {name}")
            print(output)
            print("::endgroup::")
            print(f"{status:>15}  {name}", flush=True)
            if status != "OK":
                failures.append(name)

    print()
    print(f"passed={len(names) - len(failures)} failed={len(failures)}")
    if failures:
        print(f"FAILED: {' '.join(failures)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

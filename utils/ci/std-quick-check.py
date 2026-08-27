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
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REGISTRY = REPO / "tests" / "neverc" / "StdLibTests.cpp"
TEST_DIR = REPO / "tests" / "neverc" / "std"

DIRECT_CALL = re.compile(
    r'compileAndRunStdTest\s*\(\s*"(?P<name>[^"]+)"\s*'
    r'(?:,\s*\{(?P<srcs>[^{}]*)\}\s*)?'
    r'(?:,\s*\{(?P<flags>[^{}]*)\}\s*)?\)'
)


def expand_dependency_macros(text):
    """Inline the multiline *_DEPS macros the registry uses for source lists."""
    macro = re.compile(
        r"#define\s+([A-Z][A-Z0-9_]*_DEPS)\s*\\\n((?:.*\\\n)*.*?\n)"
    )
    for match in macro.finditer(text):
        files = sorted(set(re.findall(r'"([^"]+)"', match.group(2))))
        replacement = ", ".join(f'"{f}"' for f in files)
        text = re.sub(rf"\b{re.escape(match.group(1))}\b", replacement, text)
    return text


def load_registry():
    """Return {test_name: (sources, defines)} for every registered std test.

    ``STD_TEST`` covers most of the suite, but tests whose name collides with a
    C++ keyword are wired up through a bare ``compileAndRunStdTest`` call, so
    both spellings have to be recognised.
    """
    spec = importlib.util.spec_from_file_location(
        "check_test_deps", REPO / "utils" / "lint" / "check-test-deps.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    text = REGISTRY.read_text()
    registry = {
        name: (sources, [])
        for name, sources in module.parse_tests(text).items()
    }

    for match in DIRECT_CALL.finditer(expand_dependency_macros(text)):
        sources = set(re.findall(r'"([^"]+)"', match.group("srcs") or ""))
        defines = [
            flag[2:]
            for flag in re.findall(r'"([^"]+)"', match.group("flags") or "")
            if flag.startswith("-D")
        ]
        registry.setdefault(match.group("name"), (sources, defines))
    return registry


def base_flags(cc, sanitize, extra):
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
        f"-I{TEST_DIR}",
    ]
    if sanitize and sanitize != "none":
        args.append(f"-fsanitize={sanitize}")
    args.extend(extra)
    return args


def build_archive(cc, sanitize, extra, workdir):
    """Whole-library archive for tests registered without explicit sources.

    Those tests link the full std library in CMake. The archive is only
    appended to their link line, so white-box tests that include .c files
    directly keep their own definitions and their stubs.
    """
    objdir = os.path.join(workdir, "stdlib")
    os.makedirs(objdir, exist_ok=True)
    archive = os.path.join(objdir, "libneverc_std_quick.a")
    if os.path.exists(archive):
        return archive, ""
    objects = []
    for source in sorted((REPO / "std" / "src").rglob("*.c")):
        obj = os.path.join(objdir, f"{len(objects)}_{source.stem}.o")
        build = subprocess.run(
            base_flags(cc, sanitize, extra) + ["-c", str(source), "-o", obj],
            capture_output=True,
            text=True,
        )
        if build.returncode != 0:
            return None, build.stdout + build.stderr
        objects.append(obj)
    archived = subprocess.run(
        ["ar", "rcs", archive] + objects, capture_output=True, text=True
    )
    if archived.returncode != 0:
        return None, archived.stdout + archived.stderr
    return archive, ""


def compile_command(cc, name, sources, sanitize, defines, extra, out,
                    archive=None):
    args = base_flags(cc, sanitize, extra)
    args.extend(f"-D{d}" for d in defines)
    args.append(str(TEST_DIR / f"test_{name}.c"))
    args.extend(str(REPO / "std" / source) for source in sorted(sources))
    if archive:
        args.append(archive)
    args.extend(["-lm", "-pthread", "-lresolv", "-o", out])
    return args


def run_one(cc, name, sources, sanitize, defines, extra, timeout, workdir):
    binary = os.path.join(workdir, f"test_{name}")
    archive = None
    if not sources:
        archive, error = build_archive(cc, sanitize, extra, workdir)
        if not archive:
            return "COMPILE-FAIL", error
    build = subprocess.run(
        compile_command(cc, name, sources, sanitize, defines, extra, binary,
                        archive),
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
    parser.add_argument(
        "--extra",
        action="append",
        default=[],
        help="extra compiler flag, repeatable",
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
            sources, defines = registry[name]
            status, output = run_one(
                args.cc,
                name,
                sources,
                args.sanitize,
                args.define + defines,
                args.extra,
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

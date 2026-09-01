#!/usr/bin/env python3

"""End-to-end release SDK smoke check.

Installs the packaged plugin SDK (the ``neverc-pluginsdk`` install component)
from a build directory into a throwaway prefix, consumes it from that clean
prefix exactly as an external user would (``test-installed-sdk.py``), and then
re-runs the generator, single-header, ABI, public-C, capability-binding,
coverage and global-state gates against the same source tree and built
compiler.  Running against a freshly staged prefix catches release-packaging
gaps that in-tree builds hide.

It is intentionally dependency-free (standard library only) so it can run in a
release workflow without extra packages.  Point ``--prefix`` at an already
unpacked real release archive to validate that archive instead of installing.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent


def run(cmd: list[str], *, cwd: Path | None = None) -> int:
    print("+ " + " ".join(str(part) for part in cmd), flush=True)
    return subprocess.run(cmd, cwd=None if cwd is None else str(cwd)).returncode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", required=True, type=Path,
                    help="configured build directory (source of the SDK "
                         "install component and the built compiler)")
    ap.add_argument("--prefix", type=Path, default=None,
                    help="validate this already-unpacked release prefix "
                         "instead of installing the SDK component")
    ap.add_argument("--cc", default=None, help="C compiler for the consumer")
    ap.add_argument("--cxx", default=None, help="C++ compiler for the consumer")
    ap.add_argument("--skip-build", action="store_true",
                    help="assume neverc-pluginsdk is already built")
    ap.add_argument("--keep", action="store_true",
                    help="keep the temporary install prefix")
    args = ap.parse_args()

    build = args.build_dir.resolve()
    compiler_candidates = (build / "bin" / "neverc",
                           build / "bin" / "neverc.exe")
    compiler = next((path for path in compiler_candidates if path.exists()),
                    compiler_candidates[0])
    failures: list[str] = []

    tmp: Path | None = None
    prefix = args.prefix.resolve() if args.prefix else None
    if prefix is None:
        if not args.skip_build:
            if run(["cmake", "--build", str(build),
                    "--target", "neverc-pluginsdk"]) != 0:
                failures.append("build neverc-pluginsdk")
        tmp = Path(tempfile.mkdtemp(prefix="neverc-release-sdk-"))
        prefix = tmp
        if run(["cmake", "--install", str(build),
                "--component", "neverc-pluginsdk",
                "--prefix", str(prefix)]) != 0:
            failures.append("install neverc-pluginsdk component")

    try:
        consume = [sys.executable, str(HERE / "test-installed-sdk.py"),
                   "--prefix", str(prefix)]
        if args.cc:
            consume += ["--cc", args.cc]
        if args.cxx:
            consume += ["--cxx", args.cxx]
        if run(consume) != 0:
            failures.append("test-installed-sdk")

        # Source-of-truth generators and public-header hygiene: the shipped
        # single header, manifest and ABI baseline must match the modules, and
        # the public headers must stay pure C.
        source_gates = [
            (HERE / "gen-single-header.py", ["--check"]),
            (HERE / "gen-sdk-manifest.py", ["--check"]),
            (HERE / "gen-abi-manifest.py", ["--check"]),
            (HERE / "check-single-header.py", []),
            (HERE / "check-public-c.py",
             [str(REPO / "neverc/include/neverc/Plugin")]),
        ]
        for script, extra in source_gates:
            if run([sys.executable, str(script)] + extra, cwd=REPO) != 0:
                failures.append(script.name)

        if compiler.exists():
            compiler_gates = [
                (HERE / "check-capability-bindings.py",
                 ["--compiler", str(compiler)]),
                (HERE / "check-phase-inventory.py",
                 ["--compiler", str(compiler)]),
            ]
            for script, extra in compiler_gates:
                if run([sys.executable, str(script)] + extra, cwd=REPO) != 0:
                    failures.append(script.name)

        coverage = HERE / "check-coverage.py"
        if run([sys.executable, str(coverage),
                str(REPO / "docs/plugin-api/coverage.json")],
               cwd=REPO) != 0:
            failures.append(coverage.name)

        if compiler.exists():
            global_state = HERE / "check-global-state.py"
            if run([sys.executable, str(global_state),
                    "--build-dir", str(build), "--strict"],
                   cwd=REPO) != 0:
                failures.append(global_state.name)
        else:
            tried = ", ".join(str(path) for path in compiler_candidates)
            print(f"error: built compiler not found (tried: {tried})",
                  file=sys.stderr)
            failures.append("built compiler")
    finally:
        if tmp is not None and not args.keep:
            shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("check-release-sdk: FAILED (" + ", ".join(failures) + ")",
              file=sys.stderr)
        return 1
    print("check-release-sdk: OK (release SDK consumed and all gates passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

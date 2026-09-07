#!/usr/bin/env python3
"""Run a built-in C++ protocol suite in a fresh, automatically cleaned directory."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


TARGET_TIMEOUT_SECONDS = 30
SUITE_TIMEOUT_SECONDS = 600
SUITE_FILES = {
    "core": "verify.py",
    "project": "project.py",
    "math": "math_profile.py",
}


def show_output(value, stream):
    if value:
        if isinstance(value, bytes):
            value = value.decode("utf-8", errors="replace")
        print(value, file=stream, end="" if value.endswith("\n") else "\n")


def run(command, directory, environment, timeout):
    return subprocess.run(
        command,
        cwd=directory,
        env=environment,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        check=False,
    )


def failure_status(result):
    show_output(result.stdout, sys.stdout)
    show_output(result.stderr, sys.stderr)
    return result.returncode if result.returncode > 0 else 128 - result.returncode


def suite_target(native, suite):
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.+\-]*", native) or native.count("-") < 2:
        raise ValueError("NeverC -dumpmachine did not return a single target triple")
    if suite != "math":
        return native
    if sys.platform != "darwin":
        raise ValueError("The math protocol suite is registered only on macOS hosts")
    if "-apple-" not in native or not any(part in native for part in ("-darwin", "-macos")):
        raise ValueError("The math protocol suite requires a native macOS NeverC target")
    architecture = native.split("-", 1)[0].lower()
    if architecture in ("arm64", "aarch64"):
        return "arm64-apple-macosx15.0.0"
    if architecture in ("x86_64", "amd64"):
        return "x86_64-apple-macosx15.0.0"
    raise ValueError("The math protocol suite requires an arm64 or x86_64 NeverC target")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--suite", required=True, choices=SUITE_FILES)
    args = parser.parse_args()
    binary = args.neverc.resolve()
    repository = Path(__file__).resolve().parents[2]
    script = repository / "utils/translate-frontends/cpp/tests" / SUITE_FILES[args.suite]
    label = "neverc-translate.protocol." + args.suite

    try:
        with tempfile.TemporaryDirectory(prefix="neverc-cpp-protocol-" + args.suite + "-") as temporary:
            directory = Path(temporary).resolve()
            environment = dict(os.environ)
            environment.update(
                TMPDIR=str(directory), TMP=str(directory), TEMP=str(directory),
                LC_ALL="C", NEVERC_NO_DEFAULT_CONFIG="1",
            )
            probe = run(
                [str(binary), "--no-default-config", "-dumpmachine"],
                directory, environment, TARGET_TIMEOUT_SECONDS,
            )
            if probe.returncode:
                print(label + ": target query failed", file=sys.stderr)
                return failure_status(probe)
            target = suite_target(probe.stdout.strip(), args.suite)
            # -I ignores PYTHONOPTIMIZE so the suites' assertions always run;
            # -B prevents bytecode files from being written into the checkout.
            result = run(
                [sys.executable, "-I", "-B", str(script), "--neverc", str(binary),
                 "--target", target, "--output-dir", str(directory / "results")],
                directory, environment, SUITE_TIMEOUT_SECONDS,
            )
            if result.returncode:
                print(label + ": suite failed", file=sys.stderr)
                return failure_status(result)
            show_output(result.stderr, sys.stderr)
            try:
                summary = json.loads(result.stdout)
            except json.JSONDecodeError:
                show_output(result.stdout, sys.stdout)
                raise ValueError("The protocol suite did not return its JSON result") from None
            if (not isinstance(summary, dict) or summary.get("status") != "passed"
                    or type(summary.get("cases")) is not int or summary["cases"] <= 0
                    or summary.get("target") != target):
                show_output(result.stdout, sys.stdout)
                raise ValueError("The protocol suite returned an invalid success/count/target result")
            print(f"{label}: PASS ({summary['cases']} cases; target {target})", flush=True)
            return 0
    except subprocess.TimeoutExpired as error:
        show_output(error.stdout, sys.stdout)
        show_output(error.stderr, sys.stderr)
        print(f"{label}: timed out after {error.timeout} seconds", file=sys.stderr)
        return 124
    except (OSError, ValueError) as error:
        print(f"{label}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

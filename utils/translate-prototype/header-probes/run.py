#!/usr/bin/env python3
"""Reproduce the P0 macOS SDK probes; writes measurements, never SDK contents."""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


VERSION = "20.1.8"
TARGET = "arm64-apple-macosx15.0.0"
SDK_VERSION = "15.5"
FIXTURES = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, env):
    start = time.perf_counter()
    result = subprocess.run(command, env=env, text=True, capture_output=True,
                            timeout=120, check=False)
    return result, round((time.perf_counter() - start) * 1000, 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-root", type=Path,
                        default=Path("/opt/homebrew/opt/llvm@20"))
    parser.add_argument("--sdk", type=Path, default=Path(
        "/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk"))
    parser.add_argument("--report", type=Path, required=True,
                        help="new JSON report path; existing files are refused")
    args = parser.parse_args()
    llvm_root = args.llvm_root.resolve(strict=True)
    sdk = args.sdk.resolve(strict=True)
    compiler = llvm_root / "bin/clang++"
    libcxx = llvm_root / "include/c++/v1"
    resource = llvm_root / "lib/clang/20"
    for required in (compiler, libcxx / "cmath", resource / "include/stddef.h"):
        if not required.is_file():
            parser.error(f"missing declared toolchain input: {required}")
    if args.report.exists():
        parser.error(f"report already exists: {args.report}")
    settings_path = sdk / "SDKSettings.json"
    sdk_settings = json.loads(settings_path.read_text())
    if sdk_settings.get("Version") != SDK_VERSION:
        parser.error(f"only SDK {SDK_VERSION} is measured by this probe")

    # Clang can otherwise find headers through environment and Homebrew .cfg files.
    env = os.environ.copy()
    removed_env = ("CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH",
                   "OBJC_INCLUDE_PATH", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET",
                   "CCC_OVERRIDE_OPTIONS", "CLANG_CONFIG_FILE_SYSTEM_DIR",
                   "CLANG_CONFIG_FILE_USER_DIR")
    for key in removed_env:
        env.pop(key, None)
    env["LC_ALL"] = "C"
    version, startup_ms = run([str(compiler), "--no-default-config", "--version"], env)
    first_line = version.stdout.splitlines()[0] if version.stdout else ""
    if version.returncode or not re.search(r"\bversion 20\.1\.8$", first_line):
        parser.error(f"expected Clang {VERSION}, found: {first_line}")

    flags = ["--no-default-config", "-target", TARGET, "-std=c++17",
             "-isysroot", str(sdk), "-resource-dir", str(resource),
             "-nostdinc", "-nostdinc++", "-isystem", str(libcxx),
             "-isystem", str(resource / "include"),
             "-isystem", str(sdk / "usr/include"),
             "-iframework", str(sdk / "System/Library/Frameworks")]
    roots = {"libc++": libcxx, "clang-resource": resource,
             "platform-sdk": sdk, "fixture": FIXTURES}

    def provenance(path):
        for name, root in roots.items():
            if path.is_relative_to(root):
                return name, str(path.relative_to(root))
        return "undeclared", str(path)

    report = {"measurement_schema": 1, "status": "p0-development-probe",
              "compiler_version": version.stdout.strip(),
              "compiler_sha256": digest(compiler), "target": TARGET,
              "libcpp_version_macro": 200100, "sdk_version": SDK_VERSION,
              "sdk_settings_sha256": digest(settings_path),
              "roots": {name: str(root) for name, root in roots.items()},
              "include_search_order": [str(libcxx), str(resource / "include"),
                                       str(sdk / "usr/include"),
                                       str(sdk / "System/Library/Frameworks")],
              "cleared_environment_names": list(removed_env),
              "version_startup_ms": startup_ms,
              "llvm_license": {"path": str(llvm_root / "LICENSE.TXT"),
                               "sha256": digest(llvm_root / "LICENSE.TXT")},
              "probes": []}
    failed = False
    for name in ("cmath", "string", "vector"):
        source = FIXTURES / f"{name}.cpp"
        command = [str(compiler), *flags, "-fsyntax-only", "-H", "-v", str(source)]
        result, elapsed_ms = run(command, env)
        included_paths = sorted({Path(match.group(1)).resolve()
                                 for line in result.stderr.splitlines()
                                 if (match := re.match(r"^\.+ (/.+)$", line))})
        dependencies = []
        for path in included_paths:
            distribution, relative_path = provenance(path)
            dependencies.append({"distribution": distribution, "path": relative_path,
                                 "sha256": digest(path)})
        counts = dict(sorted(Counter(d["distribution"] for d in dependencies).items()))
        passed = result.returncode == 0 and counts.get("undeclared", 0) == 0
        report["probes"].append({"name": name, "passed": passed,
                                 "exit_code": result.returncode,
                                 "syntax_only_ms": elapsed_ms,
                                 "command": command, "source_sha256": digest(source),
                                 "dependency_counts": counts,
                                 "dependencies": dependencies,
                                 "compiler_stderr": result.stderr})
        failed = failed or not passed
        print(f"{name}: {'PASS' if passed else 'FAIL'}; {elapsed_ms} ms; {counts}")
    report["passed"] = not failed
    # Measurements contain machine-specific paths and timing; these are not
    # deterministic translation manifests and must not be used as such.
    with args.report.open("x") as output:
        json.dump(report, output, indent=2, sort_keys=True)
        output.write("\n")
    print(f"Report: {args.report}")
    return int(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"header-probes: {error}", file=sys.stderr)
        sys.exit(1)

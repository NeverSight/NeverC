#!/usr/bin/env python3

"""Verify that a NeverC package loads Python plugins after relocation."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Mapping


PROBE_PLUGIN = """\
import ssl
import hashlib
import ctypes
import bz2
import lzma
import sqlite3
from neverc_plugin import Plugin

@Plugin(id="org.neverc.package-probe", name="Package Probe", version="1.0.0")
class PackageProbe:
    pass
"""
PROBE_SOURCE = "int neverc_python_package_probe(void) { return 0; }\n"
OLLVM_PROBE_SOURCE = """\
int ollvm_package_probe(int x, int y) {
  int result = x + y;
  if ((x & 1) != 0) result ^= y; else result |= y;
  return result;
}
"""
REQUIRED_PYTHON_SDK_FILES = (
    "neverc_plugin/abi.py",
    "neverc_plugin/api.py",
    "neverc_plugin/ffi.py",
    "neverc_plugin/domains/__init__.py",
    "neverc_plugin/domains/ir.py",
    "examples/ollvm/ollvm_plugin.py",
    "examples/ollvm/README.md",
)
# Assertions-off compiler builds discard IR value names by default.  The OLLVM
# package probe deliberately checks the public pass's names, so preserve them
# explicitly instead of making the result depend on the compiler build mode.
OLLVM_IR_FLAGS = ("-O0", "-fno-discard-value-names", "-S", "-emit-llvm")
HIDDEN_ENVIRONMENT = {
    "PYTHONPATH", "PYTHONHOME", "PYTHONUSERBASE", "Python3_ROOT_DIR",
    "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
    "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_FRAMEWORK_PATH", "pythonLocation",
    "Python_ROOT_DIR",
}
# ERROR_ACCESS_DENIED and ERROR_SHARING_VIOLATION. Both are emitted by
# shutil.rmtree while a scanner or another process temporarily holds a file.
WINDOWS_TRANSIENT_DELETE_ERRORS = frozenset((5, 32))
WINDOWS_DELETE_RETRY_ATTEMPTS = 12
WINDOWS_DELETE_RETRY_DELAY_SECONDS = 0.25


def fail(message: str) -> None:
    print(f"test-python-plugin-package: {message}", file=sys.stderr)


def find_prefix(root: Path) -> Path:
    candidates = [root, root / "install"]
    candidates.extend(path.parent.parent for path in root.glob("*/bin/neverc*"))
    for candidate in candidates:
        executable = candidate / "bin" / ("neverc.exe" if os.name == "nt" else "neverc")
        if executable.is_file() and (candidate / "pluginsdk" / "python").is_dir():
            return candidate.resolve()
    raise ValueError(f"no installed NeverC compiler and Python SDK found under {root}")


def sanitized_environment(source: Mapping[str, str]) -> dict[str, str]:
    hidden = {name.casefold() for name in HIDDEN_ENVIRONMENT}
    source_by_name = {key.casefold(): value for key, value in source.items()}
    environment = {
        key: value
        for key, value in source.items()
        if key.casefold() not in hidden | {"path"}
    }
    python_location = source_by_name.get("pythonlocation", "")
    path_entries: list[str] = []
    for entry in source_by_name.get("path", "").split(os.pathsep):
        normalized = entry.replace("\\", "/").casefold()
        location = python_location.replace("\\", "/").casefold()
        if not entry:
            continue
        if "/hostedtoolcache/python/" in normalized:
            continue
        if location and (normalized == location or normalized.startswith(location + "/")):
            continue
        path_entries.append(entry)
    environment["PATH"] = os.pathsep.join(path_entries)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    return environment


def environment_for_neverc(executable: Path) -> dict[str, str]:
    environment = sanitized_environment(os.environ)
    if os.name != "nt":
        return environment
    bin_dir = str(executable.parent.resolve())
    existing = environment.get("PATH", "")
    environment["PATH"] = (
        bin_dir if not existing else bin_dir + os.pathsep + existing
    )
    return environment


def run(command: list[str], *, environment: Mapping[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    cwd: Path | None = None
    if command and os.name == "nt":
        executable = Path(command[0]).resolve()
        if not executable.is_file():
            raise FileNotFoundError(f"command executable is missing: {executable}")
        command = [str(executable), *command[1:]]
        cwd = executable.parent.resolve()
    kwargs: dict[str, object] = {
        "capture_output": True,
        "text": True,
        "env": dict(environment) if environment is not None else None,
    }
    if cwd is not None:
        kwargs["cwd"] = str(cwd)
    attempts = 3 if os.name == "nt" else 1
    result: subprocess.CompletedProcess[str] | None = None
    for attempt in range(attempts):
        try:
            result = subprocess.run(command, **kwargs)  # type: ignore[arg-type]
            break
        except OSError as error:
            transient = os.name == "nt" and getattr(error, "winerror", None) == 267
            if not transient or attempt + 1 == attempts:
                raise
            time.sleep(0.5 * (attempt + 1))
    assert result is not None
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed with exit {result.returncode} "
            f"({' '.join(command)}):\n{result.stdout}{result.stderr}"
        )
    return result


def reject_build_paths(output: str, prefix: Path) -> None:
    normalized = output.replace("\\", "/")
    prefix_text = str(prefix).replace("\\", "/")
    normalized_prefix = prefix_text.casefold()
    forbidden = (
        "/hostedtoolcache/Python/", "/Users/runner/hostedtoolcache/",
        "C:/hostedtoolcache/", "C:/actions-runner/",
    )
    for value in forbidden:
        if value.casefold() in normalized.casefold():
            raise ValueError(f"loader metadata retains build-time Python path {value}")
    for line in normalized.splitlines():
        line_without_prefix = re.sub(
            re.escape(prefix_text), "", line, flags=re.IGNORECASE
        )
        if "python" not in line_without_prefix.casefold():
            continue
        absolute_only = re.sub(
            r"(?:\$ORIGIN|@executable_path|@loader_path|@rpath)(?:/[^\s\]\)]*)?",
            "",
            line_without_prefix,
        )
        for token in re.findall(
            r"(?<![A-Za-z0-9_.$@-])(?:[A-Za-z]:)?/[^\s\]\)]+",
            absolute_only,
        ):
            if token.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if normalized_prefix in token.casefold():
                continue
            raise ValueError(f"loader metadata contains absolute Python path: {token}")


def check_linux_dynamic(output: str, prefix: Path) -> None:
    reject_build_paths(output, prefix)
    needed = re.findall(r"\(NEEDED\).*?\[([^]]+)\]", output)
    python_needed = [value for value in needed if "libpython" in value.casefold()]
    if not python_needed:
        raise ValueError("NeverC has no libpython dependency")
    if any("/" in value or "\\" in value for value in python_needed):
        raise ValueError("NeverC's libpython dependency is not a basename")
    rpaths = re.findall(r"\((?:RUNPATH|RPATH)\).*?\[([^]]+)\]", output)
    if not any("$ORIGIN/../python/lib" in value for value in rpaths):
        raise ValueError("NeverC does not carry the relative Python rpath")


def check_macos_loader(executable: Path, prefix: Path, runtime: str) -> None:
    dependencies = run(["otool", "-L", str(executable)]).stdout
    reject_build_paths(dependencies, prefix)
    python_lines = [line.strip().split(" (", 1)[0] for line in dependencies.splitlines()[1:]
                    if "python" in line.casefold()]
    allowed = {f"@rpath/{runtime}"}
    if not python_lines or not all(
        value in allowed or value.startswith("@rpath/libpython")
        for value in python_lines
    ):
        raise ValueError("NeverC must import libpython through @rpath")
    commands = run(["otool", "-l", str(executable)]).stdout
    reject_build_paths(commands, prefix)
    if "@executable_path/../python/lib" not in commands:
        raise ValueError("NeverC does not carry the relative Python LC_RPATH")


def parse_pe_imports(output: str) -> list[str]:
    return re.findall(r"(?:Name:\s*)?([A-Za-z0-9_.+-]+\.dll)\b", output, re.IGNORECASE)


def check_windows_loader(executable: Path, prefix: Path, runtime: str) -> None:
    tool = shutil.which("dumpbin")
    command = [tool, "/dependents", str(executable)] if tool else []
    if not command:
        tool = shutil.which("llvm-readobj")
        command = [tool, "--coff-imports", str(executable)] if tool else []
    if not command:
        raise RuntimeError("dumpbin or llvm-readobj is required for package verification")
    output = run(command).stdout
    reject_build_paths(output, prefix)
    imports = {name.casefold() for name in parse_pe_imports(output)}
    if runtime.casefold() not in imports:
        raise ValueError(f"NeverC does not import bundled {runtime}")
    if not (prefix / "bin" / runtime).is_file():
        raise ValueError(f"bundled runtime is missing beside NeverC: {runtime}")


def check_loader(prefix: Path, executable: Path) -> None:
    manifest_path = prefix / "python" / "runtime.json"
    if not manifest_path.is_file():
        raise ValueError(f"bundled runtime manifest is missing: {manifest_path}")
    manifest_text = manifest_path.read_text(encoding="utf-8")
    reject_build_paths(manifest_text, prefix)
    manifest = json.loads(manifest_text)
    runtime = str(manifest.get("runtime", ""))
    if not runtime:
        raise ValueError("runtime.json does not name the CPython runtime")
    if sys.platform == "darwin":
        check_macos_loader(executable, prefix, runtime)
    elif os.name == "nt":
        check_windows_loader(executable, prefix, runtime)
    else:
        output = run(["readelf", "-d", str(executable)]).stdout
        check_linux_dynamic(output, prefix)


def safe_extract(archive: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive) as package:
        destination_resolved = destination.resolve()
        for member in package.infolist():
            target = (destination / member.filename).resolve()
            try:
                target.relative_to(destination_resolved)
            except ValueError as error:
                raise ValueError(f"unsafe archive member: {member.filename}") from error
        package.extractall(destination)
        if os.name != "nt":
            for member in package.infolist():
                mode = (member.external_attr >> 16) & 0o777
                target = destination / member.filename
                if mode and target.exists():
                    target.chmod(mode)


def run_plugin_probe(prefix: Path, executable: Path) -> None:
    environment = environment_for_neverc(executable)
    with managed_temporary_directory(
        prefix="neverc-python-package-probe-",
        ignore_transient_cleanup_errors=True,
    ) as work:
        plugin = work / "probe.py"
        source = work / "probe.c"
        plugin.write_text(PROBE_PLUGIN, encoding="utf-8")
        source.write_text(PROBE_SOURCE, encoding="utf-8")
        run([
            str(executable), f"-fplugin={plugin}", "-fsyntax-only", str(source)
        ], environment=environment)


def check_python_sdk(prefix: Path) -> Path:
    sdk = prefix / "pluginsdk" / "python"
    missing = [relative for relative in REQUIRED_PYTHON_SDK_FILES
               if not (sdk / relative).is_file()]
    if missing:
        raise ValueError(
            "installed Python SDK is incomplete: " + ", ".join(missing)
        )
    return sdk


def check_ollvm_ir(output: str, target: str) -> None:
    for marker in ("ollvm.sub", "ollvm.bcf.gate", "ollvm.fla.dispatch"):
        if marker not in output:
            raise ValueError(
                f"Python OLLVM package probe for {target} is missing {marker}"
            )


def run_ollvm_probe(prefix: Path, executable: Path) -> None:
    sdk = check_python_sdk(prefix)
    plugin = sdk / "examples" / "ollvm" / "ollvm_plugin.py"
    environment = environment_for_neverc(executable)
    with managed_temporary_directory(
        prefix="neverc-python-ollvm-probe-",
        ignore_transient_cleanup_errors=True,
    ) as work:
        source = work / "ollvm_probe.c"
        source.write_text(OLLVM_PROBE_SOURCE, encoding="utf-8")

        outputs: list[str] = []
        for index, target in enumerate((
            "x86_64-unknown-linux-gnu",
            "aarch64-unknown-linux-gnu",
            "x86_64-unknown-linux-gnu",
        )):
            ir = work / f"ollvm-{index}.ll"
            run([
                str(executable),
                f"-fplugin={plugin}",
                "--ollvm-sub",
                "--ollvm-bcf",
                "--ollvm-fla",
                "--ollvm-seed",
                "42",
                "--ollvm-include",
                "ollvm_package_probe",
                f"--target={target}",
                *OLLVM_IR_FLAGS,
                str(source),
                "-o",
                str(ir),
            ], environment=environment)
            output = ir.read_text(encoding="utf-8")
            check_ollvm_ir(output, target)
            outputs.append(output)
        if outputs[0] != outputs[2]:
            raise ValueError("Python OLLVM package probe is not deterministic")


def verify_prefix(prefix: Path) -> None:
    prefix = find_prefix(prefix)
    executable = (
        prefix / "bin" / ("neverc.exe" if os.name == "nt" else "neverc")
    ).resolve()
    check_loader(prefix, executable)
    run_plugin_probe(prefix, executable)
    run_ollvm_probe(prefix, executable)


def remove_tree_with_retries(
    path: Path, attempts: int = WINDOWS_DELETE_RETRY_ATTEMPTS
) -> None:
    """Remove a tree, retrying transient Windows access and sharing failures."""
    if attempts < 1:
        raise ValueError("attempts must be positive")
    for attempt in range(attempts):
        try:
            shutil.rmtree(path)
            return
        except OSError as error:
            winerror = getattr(error, "winerror", None)
            final_attempt = attempt + 1 == attempts
            if (
                os.name != "nt"
                or winerror not in WINDOWS_TRANSIENT_DELETE_ERRORS
                or final_attempt
            ):
                raise
            time.sleep(WINDOWS_DELETE_RETRY_DELAY_SECONDS * (attempt + 1))


@contextmanager
def managed_temporary_directory(
    *, prefix: str, ignore_transient_cleanup_errors: bool = False
) -> Iterator[Path]:
    """Create a temporary tree with explicit cleanup-failure semantics.

    Cleanup never masks an exception raised by the body.  Callers may also
    downgrade an exhausted Windows sharing/access violation to a warning when
    cleanup is ancillary to their successful result.
    """
    path = Path(tempfile.mkdtemp(prefix=prefix))
    body_error: BaseException | None = None
    try:
        yield path
    except BaseException as error:
        body_error = error
        raise
    finally:
        try:
            remove_tree_with_retries(path)
        except OSError as cleanup_error:
            transient_windows_lock = (
                os.name == "nt"
                and getattr(cleanup_error, "winerror", None)
                in WINDOWS_TRANSIENT_DELETE_ERRORS
            )
            if body_error is not None:
                detail = (
                    "temporary-directory cleanup also failed; preserving the "
                    f"original error: {path} ({cleanup_error})"
                )
                if hasattr(body_error, "add_note"):
                    body_error.add_note(detail)
                fail(f"warning: {detail}")
            elif ignore_transient_cleanup_errors and transient_windows_lock:
                fail(
                    "warning: operation succeeded, but Windows kept the "
                    "temporary directory locked; leaving it for runner/OS cleanup: "
                    f"{path} after {WINDOWS_DELETE_RETRY_ATTEMPTS} attempts "
                    f"({cleanup_error})"
                )
            else:
                raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--prefix", type=Path)
    source.add_argument("--archive", type=Path)
    arguments = parser.parse_args()
    try:
        if arguments.prefix:
            verify_prefix(arguments.prefix.resolve())
        else:
            archive = arguments.archive.resolve()
            with managed_temporary_directory(
                prefix="neverc-python-package-",
                ignore_transient_cleanup_errors=True,
            ) as root:
                safe_extract(archive, root)
                verify_prefix(root)
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile) as error:
        fail(str(error))
        return 1
    print(
        "test-python-plugin-package: relocated Python ABI and OLLVM plugin "
        "loaded successfully"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

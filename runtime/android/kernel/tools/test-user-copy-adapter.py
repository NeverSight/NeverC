#!/usr/bin/env python3
"""Build and run the host-side user-copy adapter behavior test."""

from __future__ import annotations

import os
import platform
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path


KERNEL_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = KERNEL_ROOT.parents[2]
TEST_SOURCE = KERNEL_ROOT / "tools" / "test-user-copy-adapter.c"


def find_compiler() -> list[str]:
    configured = os.environ.get("CC")
    if configured:
        return shlex.split(configured)

    for candidate in ("clang", "cc"):
        compiler = shutil.which(candidate)
        if compiler:
            return [compiler]
    raise SystemExit("error: no host C compiler found (set CC)")


def host_target_flags(compiler: list[str]) -> list[str]:
    """On Windows, force a target that matches the MSVC LIB arch.

    GitHub's arm64 runners often leave `clang` defaulting to x64 while
    `ilammy/msvc-dev-cmd` has pointed LIB at ARM64 CRT libs, which produces
    `machine type arm64 conflicts with x64` at link time.
    """
    if os.name != "nt":
        return []

    # Respect a target already supplied through CC.
    if any(
        arg in ("-target", "--target")
        or arg.startswith(("-target=", "--target="))
        for arg in compiler
    ):
        return []

    # VSCMD_ARG_TGT_ARCH describes the architecture selected for LIB/INCLUDE
    # and is authoritative. PROCESSOR_ARCHITECTURE describes the current
    # process instead, which may be x64 under emulation on an ARM64 runner.
    machine = (
        os.environ.get("VSCMD_ARG_TGT_ARCH")
        or os.environ.get("PROCESSOR_ARCHITECTURE")
        or platform.machine()
    ).strip().lower()

    if machine in ("arm64", "aarch64"):
        return ["--target=aarch64-pc-windows-msvc"]
    if machine in ("amd64", "x64", "x86_64"):
        return ["--target=x86_64-pc-windows-msvc"]
    raise SystemExit(
        f"error: unsupported Windows target architecture: {machine or '<empty>'}"
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="neverc-usercopy-") as temp_dir:
        executable = Path(temp_dir) / "test-user-copy-adapter"
        compiler = find_compiler()
        command = [
            *compiler,
            *host_target_flags(compiler),
            "-std=gnu11",
            "-D__KERNEL__",
            "-DMODULE",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-macro-redefined",
            "-Wno-unused-function",
            f"-I{KERNEL_ROOT / 'arm64' / 'include'}",
            f"-I{KERNEL_ROOT / 'include'}",
            f"-I{KERNEL_ROOT / 'src'}",
            f"-I{REPO_ROOT / 'neverc' / 'lib' / 'Headers'}",
            str(TEST_SOURCE),
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)

    print("user-copy adapter behavior test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

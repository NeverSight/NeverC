"""Shared host-compiler selection for executable kernel-runtime fixtures."""

from __future__ import annotations

import os
import platform
import shlex
import shutil


def find_compiler() -> list[str]:
    configured = os.environ.get("CC")
    if configured:
        compiler = shlex.split(configured)
        if compiler:
            return compiler
        raise RuntimeError("CC does not name a compiler")

    for candidate in ("clang", "cc"):
        compiler = shutil.which(candidate)
        if compiler:
            return [compiler]
    raise RuntimeError("no host C compiler found (set CC)")


def host_target_flags(compiler: list[str]) -> list[str]:
    """Match Clang's Windows target to the CRT selected by MSVC setup."""
    if os.name != "nt":
        return []
    if any(
        argument in ("-target", "--target")
        or argument.startswith(("-target=", "--target="))
        for argument in compiler
    ):
        return []

    machine = (
        os.environ.get("VSCMD_ARG_TGT_ARCH")
        or os.environ.get("PROCESSOR_ARCHITECTURE")
        or platform.machine()
    ).strip().lower()
    if machine in ("arm64", "aarch64"):
        return ["--target=aarch64-pc-windows-msvc"]
    if machine in ("amd64", "x64", "x86_64"):
        return ["--target=x86_64-pc-windows-msvc"]
    raise RuntimeError(
        f"unsupported Windows target architecture: {machine or '<empty>'}"
    )


def host_compiler_command() -> list[str]:
    compiler = find_compiler()
    return [*compiler, *host_target_flags(compiler)]

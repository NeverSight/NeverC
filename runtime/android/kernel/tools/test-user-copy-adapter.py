#!/usr/bin/env python3
"""Build and run the host-side user-copy adapter behavior test."""

from __future__ import annotations

import os
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


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="neverc-usercopy-") as temp_dir:
        executable = Path(temp_dir) / "test-user-copy-adapter"
        command = [
            *find_compiler(),
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

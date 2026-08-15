#!/usr/bin/env python3
"""Build and run the host-side user-copy adapter behavior test."""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

from host_test_compiler import host_compiler_command


KERNEL_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = KERNEL_ROOT.parents[2]
TEST_SOURCE = KERNEL_ROOT / "tools" / "test-user-copy-adapter.c"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="neverc-usercopy-") as temp_dir:
        executable = Path(temp_dir) / "test-user-copy-adapter"
        compiler = host_compiler_command()
        command = [
            *compiler,
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

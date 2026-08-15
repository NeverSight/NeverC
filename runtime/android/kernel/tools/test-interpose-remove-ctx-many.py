#!/usr/bin/env python3
"""Build and run host fault-injection tests for interpose teardown policy."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


KERNEL_ROOT = Path(__file__).resolve().parents[1]
TEST_SOURCE = KERNEL_ROOT / "tools" / "test-interpose-remove-ctx-many.c"


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
    with tempfile.TemporaryDirectory(prefix="neverc-interpose-remove-") as temp:
        executable = Path(temp) / "test-interpose-remove-ctx-many"
        command = [
            *find_compiler(),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pthread",
            str(TEST_SOURCE),
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)

    print("interpose teardown and batch rollback policy tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build and run host fault-injection tests for interpose teardown policy."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile

from host_test_compiler import host_compiler_command


KERNEL_ROOT = Path(__file__).resolve().parents[1]
TEST_SOURCE = KERNEL_ROOT / "tools" / "test-interpose-remove-ctx-many.c"


def main() -> int:
    if os.name == "nt":
        print(
            "interpose teardown and batch rollback policy tests skipped "
            "(requires POSIX pthreads)"
        )
        return 0

    with tempfile.TemporaryDirectory(prefix="neverc-interpose-remove-") as temp:
        executable = Path(temp) / "test-interpose-remove-ctx-many"
        command = [
            *host_compiler_command(),
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

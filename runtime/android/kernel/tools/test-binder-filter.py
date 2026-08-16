#!/usr/bin/env python3
"""Build and run the production Binder filter host contract."""

from pathlib import Path
import subprocess
import sys
import tempfile

from host_test_compiler import host_compiler_command


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent


def main() -> int:
    compiler = host_compiler_command()
    with tempfile.TemporaryDirectory(prefix="neverc-binder-filter-") as raw_temp:
        output = Path(raw_temp) / "test-binder-filter"
        command = [
            *compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DNEVERC_KRT_BINDER_HOST_TEST=1",
            f"-I{RUNTIME_ROOT / 'arm64/include'}",
            f"-I{RUNTIME_ROOT / 'include'}",
            f"-I{TOOLS_ROOT}",
            str(TOOLS_ROOT / "test-binder-filter.c"),
            str(RUNTIME_ROOT / "src/nvk_binder.c"),
            "-o",
            str(output),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)

    print("test-binder-filter: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"test-binder-filter: RED -- {error}", file=sys.stderr)
        raise SystemExit(1)

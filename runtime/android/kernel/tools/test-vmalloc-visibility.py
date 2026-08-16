#!/usr/bin/env python3
"""Build and run the vmalloc visibility resolver host contract."""

from pathlib import Path
import subprocess
import sys
import tempfile

from host_test_compiler import host_compiler_command


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent


def main() -> int:
    compiler = host_compiler_command()
    with tempfile.TemporaryDirectory(prefix="neverc-vmalloc-vis-") as raw_temp:
        output = Path(raw_temp) / "test-vmalloc-visibility"
        subprocess.run(
            [
                *compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{RUNTIME_ROOT / 'src'}",
                str(TOOLS_ROOT / "test-vmalloc-visibility.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)

    print("test-vmalloc-visibility: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"test-vmalloc-visibility: RED -- {error}", file=sys.stderr)
        raise SystemExit(1)

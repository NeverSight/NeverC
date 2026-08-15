#!/usr/bin/env python3
"""Build the active opaque user-page-table host contract.

The first phase syntax-checks the entire semantic fixture against a frozen
test-side API sketch.  The second phase deliberately remains RED until the
real public header and runtime source exist; after that, it compiles and runs
the same fixture against production in an injected host-kernel boundary.
"""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

from host_test_compiler import host_compiler_command


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
CONTRACT_SOURCE = TOOLS_ROOT / "test-user-ptmap-contract.c"
PUBLIC_HEADER = RUNTIME_ROOT / "include/nvk_user_ptmap.h"
RUNTIME_SOURCE = RUNTIME_ROOT / "src/nvk_user_ptmap.c"


def common_flags() -> list[str]:
    return [
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{TOOLS_ROOT}",
    ]


def syntax_check_frozen_contract(compiler: list[str]) -> None:
    subprocess.run(
        [
            *compiler,
            *common_flags(),
            "-DNEVERC_KRT_USER_PTMAP_CONTRACT_ONLY=1",
            "-fsyntax-only",
            str(CONTRACT_SOURCE),
        ],
        check=True,
    )


def check_public_handle_is_opaque(compiler: list[str], temp: Path) -> None:
    probe = temp / "opaque-probe.c"
    probe.write_text(
        "#include <nvk_user_ptmap.h>\n"
        "char forbidden[sizeof(struct neverc_krt_user_ptmap)];\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            *compiler,
            *common_flags(),
            f"-I{RUNTIME_ROOT / 'arm64/include'}",
            f"-I{RUNTIME_ROOT / 'include'}",
            "-fsyntax-only",
            str(probe),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        raise RuntimeError(
            "public struct neverc_krt_user_ptmap is concrete, not opaque"
        )


def build_and_run_real_contract(compiler: list[str]) -> None:
    with tempfile.TemporaryDirectory(prefix="neverc-user-ptmap-") as raw_temp:
        temp = Path(raw_temp)
        executable = temp / "test-user-ptmap-contract"
        check_public_handle_is_opaque(compiler, temp)
        subprocess.run(
            [
                *compiler,
                *common_flags(),
                "-DNEVERC_KRT_USER_PTMAP_HOST_TEST=1",
                f"-I{RUNTIME_ROOT / 'arm64/include'}",
                f"-I{RUNTIME_ROOT / 'include'}",
                f"-I{RUNTIME_ROOT / 'src'}",
                str(CONTRACT_SOURCE),
                str(RUNTIME_SOURCE),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def main() -> int:
    compiler = host_compiler_command()
    syntax_check_frozen_contract(compiler)
    print("test-user-ptmap-contract: contract fixture syntax OK")

    missing: list[Path] = []
    for required in (PUBLIC_HEADER, RUNTIME_SOURCE):
        if not required.is_file():
            missing.append(required)
    if missing:
        for path in missing:
            print(
                "test-user-ptmap-contract: missing "
                f"{path.relative_to(RUNTIME_ROOT.parent.parent.parent)}"
            )
        print(
            "test-user-ptmap-contract: stable RED -- active opaque "
            "user-page-table API is not implemented"
        )
        return 1

    build_and_run_real_contract(compiler)
    print("test-user-ptmap-contract: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"test-user-ptmap-contract: RED -- {error}", file=sys.stderr)
        raise SystemExit(1)

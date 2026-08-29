#!/usr/bin/env python3

"""Check that explicit-lifetime thread-local holders need no cleanup hook."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


SAFE_SOURCE = r"""
#include "neverc/Foundation/Core/ThreadLocalStorage.h"

struct NonTrivial {
  int Value = 1;
  ~NonTrivial() noexcept { Value = 0; }
};

thread_local neverc::ScopedThreadLocalValue<NonTrivial> Value;
thread_local neverc::ScopedThreadLocalStack<NonTrivial, 1> Stack;

extern "C" int exercise_safe_storage() {
  Value.emplace();
  Stack.push_back(NonTrivial{});
  const int Result = Value.get().Value + Stack.back().Value;
  Stack.pop_back();
  Value.reset();
  return Result;
}
"""


NEGATIVE_SOURCE = r"""
struct NonTrivial {
  int Value = 1;
  ~NonTrivial() noexcept { Value = 0; }
};

thread_local NonTrivial Value;

extern "C" int exercise_direct_storage() {
  return Value.Value;
}
"""


def run(command: list[str], description: str) -> str:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        detail = (result.stdout + result.stderr).strip()
        if detail:
            print(detail, file=sys.stderr)
        raise RuntimeError(f"{description} failed with exit code {result.returncode}")
    return result.stdout.strip()


def compile_source(cxx: str, include_dir: Path, source: Path, output: Path) -> None:
    run(
        [
            cxx,
            "-std=c++17",
            f"-I{include_dir}",
            "-c",
            str(source),
            "-o",
            str(output),
        ],
        f"compiling {source.name}",
    )


def inspect_symbols(nm: str, object_path: Path) -> str:
    return run(
        [nm, "--undefined-only", str(object_path)],
        f"inspecting {object_path.name}",
    )


def symbol_status(label: str, symbols: str) -> tuple[bool, bool]:
    normalized = symbols.casefold()
    has_cleanup_hook = "cxa_thread_atexit" in normalized
    has_emulated_access = "emutls_get_address" in normalized
    print(
        f"{label}: thread cleanup hook={'yes' if has_cleanup_hook else 'no'}, "
        f"emulated TLS access={'yes' if has_emulated_access else 'no'}"
    )
    return has_cleanup_hook, has_emulated_access


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", required=True, help="C++ compiler to inspect")
    parser.add_argument(
        "--include-dir",
        required=True,
        type=Path,
        help="directory containing the neverc include tree",
    )
    args = parser.parse_args()

    include_dir = args.include_dir.resolve()
    if not include_dir.is_dir():
        parser.error(f"include directory does not exist: {include_dir}")

    try:
        nm = run(
            [args.cxx, "-print-prog-name=nm"],
            "resolving the compiler's symbol inspector",
        )
        if not nm:
            raise RuntimeError("compiler returned an empty symbol-inspector path")

        with tempfile.TemporaryDirectory(
            prefix="neverc-thread-local-codegen-"
        ) as temporary:
            work = Path(temporary)
            safe_source = work / "safe.cpp"
            negative_source = work / "negative.cpp"
            safe_object = work / "safe.o"
            negative_object = work / "negative.o"

            safe_source.write_text(SAFE_SOURCE, encoding="utf-8")
            negative_source.write_text(NEGATIVE_SOURCE, encoding="utf-8")
            compile_source(args.cxx, include_dir, safe_source, safe_object)
            compile_source(args.cxx, include_dir, negative_source, negative_object)

            safe_symbols = inspect_symbols(nm, safe_object)
            negative_symbols = inspect_symbols(nm, negative_object)

        safe_cleanup, _ = symbol_status("explicit-lifetime holders", safe_symbols)
        negative_cleanup, _ = symbol_status("direct non-trivial object", negative_symbols)

        if not negative_cleanup:
            print(
                "error: the negative control did not emit a thread cleanup hook",
                file=sys.stderr,
            )
            return 1
        if safe_cleanup:
            print(
                "error: an explicit-lifetime holder emitted a thread cleanup hook",
                file=sys.stderr,
            )
            return 1
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print("thread-local code generation check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

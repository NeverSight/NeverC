#!/usr/bin/env python3
"""Reject unsafe Android kernel runtime user-copy backends and bypasses."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


RUNTIME_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = RUNTIME_ROOT / "src"
USERCOPY_SOURCE = SOURCE_ROOT / "nvk_usercopy.c"

FORBIDDEN_SYMBOLS = (
    "_copy_from_user",
    "_copy_to_user",
    "raw_copy_from_user",
    "raw_copy_to_user",
    "copy_from_user",
    "copy_to_user",
)
FORBIDDEN_STRING = re.compile(
    r'"(?:' + "|".join(re.escape(name) for name in FORBIDDEN_SYMBOLS) + r')"'
)
LEGACY_IDENTIFIERS = (
    "neverc_krt_copy_from_user_fn",
    "neverc_krt_copy_to_user_fn",
    "_neverc_krt_copy_from_user",
    "_neverc_krt_copy_to_user",
)
LEGACY_IDENTIFIER = re.compile(
    r"\b(?:"
    + "|".join(re.escape(name) for name in LEGACY_IDENTIFIERS)
    + r")\b"
)
REQUIRED_BACKENDS = (
    "simple_read_from_buffer",
    "simple_write_to_buffer",
)
ARTIFACT_REQUIRED_SYMBOLS = frozenset(
    {
        "neverc_krt_mem_read_user",
        "neverc_krt_mem_write_user",
        "_neverc_krt_mem_copy_from_user_compat",
        "_neverc_krt_mem_copy_to_user_compat",
        "_neverc_krt_simple_read_from_buffer",
        "_neverc_krt_simple_write_to_buffer",
    }
)
ARTIFACT_FORBIDDEN_SYMBOLS = frozenset(
    {
        "_neverc_krt_copy_from_user",
        "_neverc_krt_copy_to_user",
    }
)
RUNTIME_LOCAL_PREFIX = "__neverc_nvk_local."


def line_number(text, offset):
    return text.count("\n", 0, offset) + 1


def add_matches(violations, path, text, pattern, message):
    for match in pattern.finditer(text):
        violations.append(
            (path, line_number(text, match.start()), message.format(
                token=match.group(0)
            ))
        )


def normalize_artifact_symbol(symbol):
    if symbol.startswith(RUNTIME_LOCAL_PREFIX):
        return symbol[len(RUNTIME_LOCAL_PREFIX):]
    return symbol


def parse_defined_symbols(nm_output):
    symbols = set()
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        symbol_type = fields[-2]
        if len(symbol_type) != 1 or symbol_type in {"U", "u"}:
            continue
        symbols.add(normalize_artifact_symbol(fields[-1]))
    return symbols


def artifact_symbol_violations(symbols):
    violations = []
    for symbol in sorted(ARTIFACT_FORBIDDEN_SYMBOLS.intersection(symbols)):
        violations.append(
            f"legacy user-copy definition is forbidden: {symbol}"
        )
    for symbol in sorted(ARTIFACT_REQUIRED_SYMBOLS.difference(symbols)):
        violations.append(
            f"required user-copy definition is missing: {symbol}"
        )
    return violations


def check_source_policy():
    violations = []

    for path in sorted(SOURCE_ROOT.glob("*.c")):
        text = path.read_text(encoding="utf-8")
        add_matches(
            violations,
            path,
            text,
            FORBIDDEN_STRING,
            "must not resolve or name unsafe user-copy backend {token}",
        )
        add_matches(
            violations,
            path,
            text,
            LEGACY_IDENTIFIER,
            "legacy raw user-copy state/call bypass is forbidden: {token}",
        )

    if not USERCOPY_SOURCE.is_file():
        violations.append(
            (
                USERCOPY_SOURCE,
                1,
                "dedicated user-copy backend source is missing",
            )
        )
    else:
        usercopy_text = USERCOPY_SOURCE.read_text(encoding="utf-8")
        for backend in REQUIRED_BACKENDS:
            if f'"{backend}"' not in usercopy_text:
                violations.append(
                    (
                        USERCOPY_SOURCE,
                        1,
                        "required stable user-copy backend is missing: "
                        f"{backend}",
                    )
                )

    return violations


def run_source_policy():
    violations = check_source_policy()
    if violations:
        for path, line, message in violations:
            print(
                f"{path.relative_to(RUNTIME_ROOT)}:{line}: {message}",
                file=sys.stderr,
            )
        print(
            f"user-copy backend check failed: {len(violations)} violation(s)",
            file=sys.stderr,
        )
        return 1

    print("user-copy backend check passed")
    return 0


def run_artifact_policy(artifact, nm_tool):
    artifact_path = Path(artifact)
    if not artifact_path.is_file():
        print(
            f"error: artifact not found: {artifact_path}",
            file=sys.stderr,
        )
        return 1

    try:
        result = subprocess.run(
            [nm_tool, "-a", str(artifact_path)],
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        print(f"error: nm tool not found: {nm_tool}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip()
        if detail:
            print(detail, file=sys.stderr)
        print(
            f"error: {nm_tool} failed for {artifact_path} "
            f"(exit {error.returncode})",
            file=sys.stderr,
        )
        return 1

    violations = artifact_symbol_violations(
        parse_defined_symbols(result.stdout)
    )
    if violations:
        for violation in violations:
            print(f"{artifact_path}: {violation}", file=sys.stderr)
        print(
            "user-copy artifact check failed: "
            f"{len(violations)} violation(s)",
            file=sys.stderr,
        )
        return 1

    print(f"user-copy artifact check passed: {artifact_path}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--artifact",
        metavar="KO",
        help="check defined user-copy symbols in a compiled module",
    )
    parser.add_argument(
        "--nm",
        default=os.environ.get("NM", "nm"),
        metavar="TOOL",
        help="nm-compatible tool used by artifact mode (default: NM or nm)",
    )
    args = parser.parse_args(argv)

    if args.artifact:
        return run_artifact_policy(args.artifact, args.nm)
    return run_source_policy()


if __name__ == "__main__":
    raise SystemExit(main())

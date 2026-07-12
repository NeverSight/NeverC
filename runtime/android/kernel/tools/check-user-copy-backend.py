#!/usr/bin/env python3
"""Reject unsafe Android kernel runtime user-copy backends and bypasses."""

from pathlib import Path
import re
import sys


RUNTIME_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = RUNTIME_ROOT / "src"
MEM_SOURCE = SOURCE_ROOT / "nvk_mem.c"

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


def line_number(text, offset):
    return text.count("\n", 0, offset) + 1


def add_matches(violations, path, text, pattern, message):
    for match in pattern.finditer(text):
        violations.append(
            (path, line_number(text, match.start()), message.format(
                token=match.group(0)
            ))
        )


def main():
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

    mem_text = MEM_SOURCE.read_text(encoding="utf-8")
    for backend in REQUIRED_BACKENDS:
        if f'"{backend}"' not in mem_text:
            violations.append(
                (
                    MEM_SOURCE,
                    1,
                    f"required stable user-copy backend is missing: {backend}",
                )
            )

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


if __name__ == "__main__":
    raise SystemExit(main())

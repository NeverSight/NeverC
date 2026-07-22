#!/usr/bin/env python3

"""Lint the public NeverC plugin headers for pure-C ABI hygiene.

The public ABI must be valid C11/C23 and C++17 and must not leak C++ types,
namespaces, templates, enum-typed fields, or ad-hoc packing pragmas across the
plugin boundary. This scans the public modular headers, the generated single
header, and the schema fragments (skipping the host-side ``Host/`` helpers,
which are intentionally C++) and rejects violations after stripping comments and
string literals so it does not trip on prose.

It also inspects every ``typedef struct/union { ... } Neverc*;`` body and rejects
non-fixed-width integer fields (bare ``int``/``long``/``short``/``unsigned``/
``signed``/``float``/``double``): ABI fields must use ``uint32_t``-style
fixed-width types so a struct's layout is identical across compilers and
platforms. ``char`` is permitted only for ``const char *`` string views.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Tokens that must never appear in the public C ABI once comments/strings are
# removed. Each maps to a human-readable reason.
FORBIDDEN = [
    (re.compile(r"\bstd::"), "C++ std:: symbol"),
    (re.compile(r"\bllvm::"), "LLVM C++ symbol"),
    (re.compile(r"\bnamespace\b"), "C++ namespace"),
    (re.compile(r"\btemplate\b"), "C++ template"),
    (re.compile(r"\bclass\b"), "C++ class"),
    (re.compile(r"\bwchar_t\b"), "non-portable wchar_t"),
    (re.compile(r"\benum\b"), "enum type (use a fixed-width typedef + macros)"),
    (re.compile(r"\btypename\b"), "C++ typename"),
    (re.compile(r"\bconstexpr\b"), "C++ constexpr"),
]

# Packing pragmas are only allowed inside PluginCore.h, wrapped by the paired
# NEVERC_ABI_PACK_BEGIN/END macros the SDK restores.
PACK_PRAGMA = re.compile(r"#\s*pragma\s+pack|__pragma\s*\(\s*pack|_Pragma\s*\(\s*\"?pack")

# Every "typedef struct/union { BODY } NevercName;" pair, bounded by its braces.
STRUCT_TYPEDEF = re.compile(
    r"typedef\s+(?:struct|union)\b[^{;]*\{(.*?)\}\s*(Neverc\w+)\s*;", re.DOTALL
)

# Non-fixed-width integer types must never type an ABI field.
NON_FIXED_WIDTH = re.compile(
    r"\b(int|long|short|unsigned|signed|float|double)\b"
)


def strip_comments_and_strings(text: str) -> str:
    """Replace comments and string/char literals with spaces (preserving lines)."""
    result = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        two = text[i : i + 2]
        if two == "/*":
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            result.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif two == "//":
            j = text.find("\n", i)
            j = n if j == -1 else j
            result.append(" " * (j - i))
            i = j
        elif c in ('"', "'"):
            quote = c
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                j += 1
            result.append(" " * (j - i))
            i = j
        else:
            result.append(c)
            i += 1
    return "".join(result)


def iter_headers(root: Path):
    if root.is_file():
        yield root
        return
    for path in sorted(root.glob("*.h")):
        yield path
    for path in sorted((root / "Schema").glob("*.inc")):
        yield path


def check_file(path: Path) -> list[str]:
    findings = []
    raw = path.read_text(encoding="utf-8")
    code = strip_comments_and_strings(raw)
    for lineno, line in enumerate(code.splitlines(), start=1):
        for pattern, reason in FORBIDDEN:
            if pattern.search(line):
                findings.append(f"{path}:{lineno}: {reason}")
        # Packing pragmas are only allowed as part of the SDK's own paired
        # NEVERC_ABI_PACK_* macro machinery (which restores the caller's
        # packing), whether in PluginCore.h or inlined into the single header.
        if PACK_PRAGMA.search(line) and "NEVERC_ABI_" not in line:
            findings.append(
                f"{path}:{lineno}: raw packing pragma outside the "
                "NEVERC_ABI_PACK_* macros"
            )

    # ABI struct/union fields must be fixed-width (or const char * views).
    for match in STRUCT_TYPEDEF.finditer(code):
        body = match.group(1)
        struct_name = match.group(2)
        body_start_line = code.count("\n", 0, match.start(1)) + 1
        for offset_line, body_line in enumerate(body.splitlines()):
            hit = NON_FIXED_WIDTH.search(body_line)
            if hit:
                findings.append(
                    f"{path}:{body_start_line + offset_line}: non-fixed-width "
                    f"type '{hit.group(1)}' in ABI struct {struct_name} "
                    "(use a fixed-width type such as uint32_t)"
                )
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "roots",
        nargs="*",
        default=["neverc/include/neverc/Plugin"],
        help="header directories or files to scan",
    )
    arguments = parser.parse_args()

    findings: list[str] = []
    scanned = 0
    for root in arguments.roots:
        for header in iter_headers(Path(root)):
            scanned += 1
            findings.extend(check_file(header))

    if scanned == 0:
        print("check-public-c: no headers found", file=sys.stderr)
        return 1
    if findings:
        print("check-public-c: public ABI purity violations:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1
    print(f"check-public-c: {scanned} public headers are clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

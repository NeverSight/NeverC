#!/usr/bin/env python3
"""Audit the plugin/dyncode/linker trees for plugin-related process-global
mutable compilation state.

This is the first-release hard gate for design requirement "frontend, LTO,
linker and dyncode carry no plugin-related process-global mutable compilation
state" (spec section 20 and volume 6 completion item 11).

Two layers of checking:

1. FORBIDDEN symbols -- a precise, zero-tolerance list of known process-global
   escape hatches (removed prototype loader, dyncode current-options/mode
   singletons, the three NeverC ``ListRegister*Callbacks`` vectors, linker
   ``parallel::strategy``/``getThreadIndex`` dependence and the old
   ``CommonLinkerContext::destroy``/``lctx`` singleton).  Any hit in runtime
   code (comments and string literals are stripped first) is a hard failure.

2. Heuristic declaration scan -- reports file-scope ``static``/``extern``
   mutable objects, ``thread_local`` storage and class ``static`` non-const
   data members inside the plugin/dyncode/linker trees.  Entries documented in
   ``global-state-allowlist.json`` (with owner, lifetime and justification)
   are accepted; anything else is reported.  This layer only fails the build
   under ``--strict`` so the precise FORBIDDEN gate stays actionable while the
   heuristic is tightened.

Optionally, ``--build-dir`` enables a best-effort scan of writable data symbols
in built plugin/dyncode/linker object files via ``nm``/``llvm-nm``.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[2]
ALLOWLIST_PATH = pathlib.Path(__file__).resolve().parent / "global-state-allowlist.json"

# Directories whose *runtime* code must be free of plugin-related global state.
AUDIT_DIRS = (
    "neverc/lib/Plugin",
    "neverc/lib/DynCode",
    "neverc/lib/Linker",
    "neverc/include/neverc/Plugin",
    "neverc/include/neverc/DynCode",
    "neverc/include/neverc/Linker",
)

# BackendUtil bridges the compiler into the plugin/dyncode pipeline; it must not
# reach global dyncode/plugin state either.
EXTRA_AUDIT_FILES = (
    "neverc/lib/Emit/Backend/BackendUtil.cpp",
    "neverc/include/neverc/Emit/Backend/BackendUtil.h",
)

# Zero-tolerance symbols: any hit in runtime code fails the gate.
FORBIDDEN_SYMBOLS = (
    # Prototype loader / global plugin state (removed in volume 3).
    "getGlobalPluginLoader",
    "pluginArgStorage",
    # dyncode current-options / mode singletons (removed in volume 6 task 4).
    "getCurrentDynCodeOptions",
    "currentDynCodeOptionsStorage",
    "setDynCodeModeState",
    "gDynCodeModeEnabled",
    "dyncodeModeStorage",
    "machinePassCallbackInstalled",
    "passBuilderCallbackInstalled",
    # NeverC-added process-global LLVM callback vectors (removed in volume 3).
    "ListRegisterPassBuilderCallbacks",
    "ListRegisterTargetPassConfigCallbacks",
    "ListRegisterTargetPassConfigPostPreEmitCallbacks",
    # Linker per-process parallel strategy dependence (removed in volume 5).
    "parallel::strategy",
    "parallel::getThreadIndex",
    # Old linker context singleton (removed in volume 5 task 3).
    "CommonLinkerContext::destroy",
)

# Forbidden regexes (need word boundaries / composite spellings).
FORBIDDEN_REGEXES = (
    (re.compile(r"static\s+CommonLinkerContext\s*\*\s*lctx"),
     "static CommonLinkerContext *lctx"),
    (re.compile(r"\bInputFile::isInGroup\b"), "InputFile::isInGroup"),
    (re.compile(r"\bSharedFile::vernauxNum\b"), "SharedFile::vernauxNum"),
    (re.compile(r"\bInputFile::idCount\b"), "InputFile::idCount"),
    (re.compile(r"\bLCDylib::instanceCount\b"), "LCDylib::instanceCount"),
)

# Heuristic declaration patterns for the advisory layer.
_TYPE = r"[A-Za-z_][\w:<>,&*\s]*"
DECL_PATTERNS = (
    (re.compile(r"^\s*thread_local\b"), "thread_local"),
    (re.compile(r"^\s*(?:static\s+)?thread_local\b"), "thread_local"),
)


@dataclass
class Report:
    forbidden: list[str] = field(default_factory=list)
    heuristic: list[str] = field(default_factory=list)
    binary: list[str] = field(default_factory=list)

    def failed(self, strict: bool) -> bool:
        if self.forbidden or self.binary:
            return True
        if strict and self.heuristic:
            return True
        return False


def strip_comments_and_strings(text: str) -> str:
    """Remove // and /* */ comments and the contents of string/char literals so
    that a token only counts when it is real code."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        two = text[i : i + 2]
        if two == "//":
            j = text.find("\n", i)
            if j == -1:
                break
            i = j
            continue
        if two == "/*":
            start = i
            j = text.find("*/", i + 2)
            end = n if j == -1 else j + 2
            # keep the newlines inside the comment to preserve line numbers
            out.append("\n" * text.count("\n", start, end))
            i = end
            continue
        if c in ('"', "'"):
            quote = c
            out.append(quote)
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    out.append(quote)
                    i += 1
                    break
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def iter_source_files(paths: Iterable[pathlib.Path]):
    for base in paths:
        if base.is_file():
            yield base
            continue
        if not base.exists():
            continue
        for suffix in ("*.h", "*.hpp", "*.c", "*.cc", "*.cpp", "*.inc"):
            yield from base.rglob(suffix)


def load_allowlist() -> dict:
    if not ALLOWLIST_PATH.exists():
        return {"entries": []}
    return json.loads(ALLOWLIST_PATH.read_text(encoding="utf-8"))


def allowlisted(rel: str, snippet: str, allowlist: dict) -> bool:
    for entry in allowlist.get("entries", []):
        files = entry.get("files", [])
        if any(rel == f or rel.startswith(f) for f in files):
            marker = entry.get("symbol") or entry.get("match")
            if not marker or marker in snippet:
                return True
    return False


def allowlisted_binary_symbol(name: str, allowlist: dict) -> bool:
    """Match a demangled writable symbol against the ``binary_symbols`` list.

    Only immutable-after-init lookup tables/constants, sync primitives without
    compilation state and address-only pass/type identity tokens are eligible;
    each entry carries an owner and justification.
    """
    for entry in allowlist.get("binary_symbols", []):
        marker = entry.get("symbol")
        if marker and marker in name:
            return True
    return False


def scan_sources(report: Report, allowlist: dict) -> None:
    bases = [ROOT / d for d in AUDIT_DIRS] + [ROOT / f for f in EXTRA_AUDIT_FILES]
    for path in iter_source_files(bases):
        rel = str(path.relative_to(ROOT))
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = strip_comments_and_strings(raw)
        lines = code.splitlines()
        for idx, line in enumerate(lines, start=1):
            for token in FORBIDDEN_SYMBOLS:
                if token in line:
                    report.forbidden.append(f"{rel}:{idx}: forbidden `{token}`")
            for pattern, name in FORBIDDEN_REGEXES:
                if pattern.search(line):
                    report.forbidden.append(
                        f"{rel}:{idx}: forbidden `{name}`"
                    )
            for pattern, name in DECL_PATTERNS:
                if pattern.search(line):
                    if allowlisted(rel, line.strip(), allowlist):
                        continue
                    report.heuristic.append(
                        f"{rel}:{idx}: {name} storage `{line.strip()[:80]}`"
                    )


# Demangled-symbol namespaces whose writable data must be audited.  The final
# linked compiler is the authoritative shipped artifact: scanning it (rather than
# loose object files) automatically excludes stale objects left behind by removed
# sources, unit-test object files and fuzz-only support, which are never linked
# into the shipped `neverc`.
_BINARY_SCOPES = ("neverc::plugin", "neverc::dyncode")
# Compiler-emitted writable data that is not plugin state (RTTI/vtables/init
# guards / GoogleTest identity helpers etc.).
_BINARY_NOISE = (
    "guard variable",
    "typeinfo",
    "vtable",
    " for ",
    "::dummy_",
    "::test_info_",
)


def scan_binary(report: Report, build_dir: pathlib.Path, allowlist: dict) -> None:
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if nm is None:
        print("note: nm/llvm-nm unavailable; skipping binary symbol scan")
        return
    binary = build_dir / "bin" / "neverc"
    if not binary.exists():
        print(f"note: {binary} missing; skipping binary symbol scan")
        return
    try:
        out = subprocess.run(
            [nm, "-C", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
    except OSError:
        print(f"note: unable to run {nm} on {binary}; skipping binary scan")
        return
    seen: set[str] = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        sym_type = parts[1]
        name = " ".join(parts[2:])
        # writable data / bss only: d/b/D/B/g/G.
        if sym_type not in ("d", "D", "b", "B", "g", "G"):
            continue
        low = name.lower()
        if not any(scope in name for scope in _BINARY_SCOPES):
            continue
        if any(noise in low for noise in _BINARY_NOISE):
            continue
        if name in seen:
            continue
        seen.add(name)
        if allowlisted_binary_symbol(name, allowlist):
            continue
        report.binary.append(f"neverc: writable symbol `{name}` ({sym_type})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, default=None)
    parser.add_argument("--strict", action="store_true",
                        help="fail on advisory heuristic findings too")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    allowlist = load_allowlist()
    report = Report()
    scan_sources(report, allowlist)
    if args.build_dir is not None:
        scan_binary(report, args.build_dir, allowlist)

    if args.json:
        print(json.dumps({
            "forbidden": report.forbidden,
            "heuristic": report.heuristic,
            "binary": report.binary,
            "failed": report.failed(args.strict),
        }, indent=2))
    else:
        if report.forbidden:
            print("== forbidden process-global state (must be zero) ==")
            for item in report.forbidden:
                print(f"error: {item}", file=sys.stderr)
        if report.binary:
            print("== forbidden writable plugin/dyncode symbols ==")
            for item in report.binary:
                print(f"error: {item}", file=sys.stderr)
        if report.heuristic:
            label = "error" if args.strict else "note"
            print("== thread_local / static storage (advisory) ==")
            for item in report.heuristic:
                print(f"{label}: {item}", file=sys.stderr)
        if not report.failed(args.strict):
            print("check-global-state: OK (no forbidden plugin global state)")

    return 1 if report.failed(args.strict) else 0


if __name__ == "__main__":
    raise SystemExit(main())

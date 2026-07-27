#!/usr/bin/env python3
"""Audit the plugin/dyncode/linker trees for plugin-related process-global
mutable compilation state.

This is the first-release hard gate for the design requirement that "frontend,
LTO, linker and dyncode carry no plugin-related process-global mutable
compilation state".

Two layers of checking:

1. FORBIDDEN symbols -- a precise, zero-tolerance list of known process-global
   escape hatches (removed prototype loader, dyncode current-options/mode
   singletons, the three NeverC ``ListRegister*Callbacks`` vectors, linker
   ``parallel::strategy``/``getThreadIndex`` dependence and the old
   ``CommonLinkerContext::destroy``/``lctx`` singleton).  Any hit in runtime
   code (comments and string literals are stripped first) is a hard failure.

2. Heuristic declaration scan -- reports ``thread_local`` storage declared in
   the plugin/dyncode/linker trees.  Entries documented in
   ``global-state-allowlist.json`` (with owner, lifetime and justification)
   are accepted; anything else is reported.  This layer only fails the build
   under ``--strict`` so the precise FORBIDDEN gate stays actionable while the
   heuristic is tightened.  File-scope mutable statics are deliberately left
   to the binary layer below: a source-level regex cannot tell a mutable
   global from a constant-initialized table, whereas the section a symbol
   lands in can.

Optionally, ``--build-dir`` scans the writable data symbols of the linked
compiler (``<build-dir>/bin/neverc``) via ``nm``/``llvm-nm``.  This is the
layer that catches file-scope mutable statics, and it is a hard failure; it
self-skips when ``nm`` or the binary is unavailable.
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
    # Prototype loader / global plugin state, since removed.
    "getGlobalPluginLoader",
    "pluginArgStorage",
    # dyncode current-options / mode singletons, since removed.
    "getCurrentDynCodeOptions",
    "currentDynCodeOptionsStorage",
    "setDynCodeModeState",
    "gDynCodeModeEnabled",
    "dyncodeModeStorage",
    "machinePassCallbackInstalled",
    "passBuilderCallbackInstalled",
    # NeverC-added process-global LLVM callback vectors, since removed.
    "ListRegisterPassBuilderCallbacks",
    "ListRegisterTargetPassConfigCallbacks",
    "ListRegisterTargetPassConfigPostPreEmitCallbacks",
    # Linker per-process parallel strategy dependence, since removed.
    "parallel::strategy",
    "parallel::getThreadIndex",
    # Old linker context singleton, since removed.
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

# Heuristic declaration patterns for the advisory layer.  Keep these mutually
# exclusive: a line matching two patterns would be reported twice.
DECL_PATTERNS = (
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
#
# The linker tree is deliberately absent: it lives in namespace ``linker::``
# (not ``neverc::``) and is mostly vendored LLD, whose own target/output
# singletons are not plugin state.  Its de-globalisation is pinned by the
# FORBIDDEN entries above instead of by a blanket writable-symbol scan.
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

# nm's one-letter class is only a reliable writability signal on Mach-O.  On
# ELF, ``.data.rel.ro`` is a SHF_WRITE section and therefore reported as `d`,
# even though the dynamic loader maps it read-only once relocations are applied
# (RELRO).  Every constant table holding a function pointer lands there, so
# classifying by letter alone would flag all of them.  Nothing dynamically
# initialized can live in ``.data.rel.ro`` -- the initializer would run after
# the segment is already read-only -- so such objects go to ``.data``/``.bss``
# and are still caught below.
_WRITABLE_CLASSES = ("d", "D", "b", "B", "g", "G")
_WRITABLE_ELF_PREFIXES = (".data", ".bss", ".tdata", ".tbss")


def symbol_is_writable(sym_class: str, section: str) -> bool:
    """Decide whether a symbol lives in storage that stays writable at runtime.

    ``section`` is the name reported by ``nm --format=sysv``; it is empty for
    Mach-O (and for any nm that cannot report sections), in which case the
    one-letter class is used instead.
    """
    if section.startswith("."):
        if section.startswith(".data.rel.ro"):
            return False
        return any(
            section == prefix or section.startswith(prefix + ".")
            for prefix in _WRITABLE_ELF_PREFIXES
        )
    return sym_class in _WRITABLE_CLASSES


def _nm_symbols(nm: str, binary: pathlib.Path):
    """Yield ``(name, class, section)`` triples for *binary*.

    Prefers the sysv listing because it carries a section column; falls back to
    the default listing (section left empty) when nm does not support it.
    """
    try:
        sysv = subprocess.run(
            [nm, "-C", "--format=sysv", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        sysv = None
    if sysv is not None and sysv.returncode == 0:
        rows = []
        for line in sysv.stdout.splitlines():
            fields = [field.strip() for field in line.split("|")]
            if len(fields) < 7 or not fields[0] or fields[0] == "Name":
                continue
            rows.append((fields[0], fields[2], fields[6]))
        if rows:
            return rows
    try:
        out = subprocess.run(
            [nm, "-C", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        ).stdout
    except OSError:
        return None
    rows = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        rows.append((" ".join(parts[2:]), parts[1], ""))
    return rows


def scan_binary(report: Report, build_dir: pathlib.Path, allowlist: dict) -> None:
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if nm is None:
        print("note: nm/llvm-nm unavailable; skipping binary symbol scan")
        return
    binary = build_dir / "bin" / "neverc"
    if not binary.exists():
        print(f"note: {binary} missing; skipping binary symbol scan")
        return
    symbols = _nm_symbols(nm, binary)
    if symbols is None:
        print(f"note: unable to run {nm} on {binary}; skipping binary scan")
        return
    seen: set[str] = set()
    for name, sym_type, section in symbols:
        if not symbol_is_writable(sym_type, section):
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
        where = section or sym_type
        report.binary.append(f"neverc: writable symbol `{name}` ({where})")


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

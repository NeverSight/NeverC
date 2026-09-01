#!/usr/bin/env python3
"""Audit the plugin/dyncode/linker trees for plugin-related process-global
mutable compilation state.

This is the first-release hard gate for the design requirement that "frontend,
LTO, linker and dyncode carry no plugin-related process-global mutable
compilation state".

Four complementary layers of checking:

1. FORBIDDEN symbols -- a precise, zero-tolerance list of known process-global
   escape hatches (removed prototype loader, dyncode current-options/mode
   singletons, the three NeverC ``ListRegister*Callbacks`` vectors, linker
   ``parallel::strategy``/``getThreadIndex`` dependence and the old
   ``CommonLinkerContext::destroy``/``lctx`` singleton).  Any hit in runtime
   code (comments and string literals are stripped first) is a hard failure.

2. Exact TLS declaration scan -- reports ``thread_local`` storage declared in
   the plugin/dyncode/linker trees.  Entries documented in
   ``global-state-allowlist.json`` (with owner, lifetime and justification)
   are accepted; anything else is reported.  This is the only layer that judges
   thread-local storage, because it can pin a declaration to a file, an owner
   and a clearing test -- none of which a symbol name carries.  It fails the
   build under ``--strict``, which the workflows pass.

3. Source provenance scans -- hard failures for mutable state defined with
   internal linkage in a header, and for unqualified mutable storage in audited
   implementation files whose final symbol lacks path or namespace provenance.
   Exact reviewed manifests cover immutable lookup data and address sentinels;
   every manifest entry must match exactly one declaration.  The implementation
   scan is deliberately lexical: macro-generated declarations remain a known
   limitation until the gate uses compile_commands and Clang AST locations.

   Header internal-linkage state is especially dangerous.  An anonymous
   namespace or namespace-scope ``static`` object exists once per *including
   translation unit*, so a flag set through one includer reads unset through
   the next.  This includes ``inline static`` where ``static`` silently wins.
   This tree is a header-only port of LLVM, so state upstream keeps in a .cpp
   -- where an anonymous namespace is exactly right -- now sits in headers the
   plugin path includes, where it means the opposite.  The binary layer below
   cannot recover the source path for a bare ``(anonymous namespace)::X``.
   Constant-initialized data is exempt only through the conservative scalar
   rule or an exact reviewed manifest.

4. Optionally, ``--build-dir`` scans writable data symbols of the linked
compiler (``<build-dir>/bin/neverc``). ELF/Mach-O use ``nm``/``llvm-nm``;
PE uses ``llvm-readobj`` section/symbol facts plus ``llvm-undname`` owner
demangling, with the Windows system DbgHelp undecorator as a strict fallback.
This is the artifact-level backstop for owner-qualified NeverC and
linker symbols, and it is a hard failure. A PE must embed a complete COFF static
symbol table: an ordinary MSVC image carrying only PDB/CodeView data is
reported as unscannable, never clean. A missing compiler or required tool is
likewise reported because ``--build-dir`` is a request for this scan, and an
unavailable input that prints "skipping" is indistinguishable from a clean run.
An empty or stripped static symbol table is also unscannable: the scan requires
both an external artifact sentinel and a local nlist sentinel before it trusts
the table as evidence. Thread-local symbols are excluded here and left to layer
2: TLS is per-thread rather than process-global, and its symbol layout differs
enough between platforms that judging it here made the gate reach opposite
verdicts on ELF and Mach-O for the same source.

Without ``--build-dir`` the checker is intentionally a conservative source-
only preflight: it retains every lexical finding that only a linked symbol
table can discharge, so ``--strict`` is not required to be clean in that mode.
Release qualification always supplies ``--build-dir`` and delegates source
findings only after the artifact table and both completeness sentinels were
successfully audited.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from enum import Enum
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
    "neverc/include/neverc/Foundation/Core/LLVMTimeTraceRootLease.h",
    "neverc/lib/Foundation/Core/LLVMTimeTraceRootLease.cpp",
    "neverc/lib/Foundation/Core/ProcessResourceBroker.cpp",
    "neverc/lib/Merge/Common/MergerCommon.h",
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


def forbidden_symbol_pattern(symbol: str) -> re.Pattern:
    """Match a qualified C++ name even when ``::`` is whitespace-split."""
    body = r"\s*::\s*".join(
        re.escape(component) for component in symbol.split("::"))
    return re.compile(
        rf"(?<![A-Za-z0-9_]){body}(?![A-Za-z0-9_])")


FORBIDDEN_SYMBOL_PATTERNS = tuple(
    (forbidden_symbol_pattern(symbol), symbol)
    for symbol in FORBIDDEN_SYMBOLS
)

# Forbidden regexes (need word boundaries / composite spellings).
FORBIDDEN_REGEXES = (
    (re.compile(r"static\s+CommonLinkerContext\s*\*\s*lctx\b"),
     "static CommonLinkerContext *lctx"),
    (re.compile(r"\bInputFile\s*::\s*isInGroup\b"),
     "InputFile::isInGroup"),
    (re.compile(r"\bSharedFile\s*::\s*vernauxNum\b"),
     "SharedFile::vernauxNum"),
    (re.compile(r"\bInputFile\s*::\s*idCount\b"), "InputFile::idCount"),
    (re.compile(r"\bLCDylib\s*::\s*instanceCount\b"),
     "LCDylib::instanceCount"),
)

# All supported TLS storage spellings share this matcher. Detection and exact
# allowlisting must agree: a spelling that can be reported but never named in
# the allowlist makes the ownership/lifetime contract impossible to satisfy.
_TLS_STORAGE = re.compile(
    r"\b(?:thread_local|_Thread_local|__thread|LLVM_THREAD_LOCAL)\b|"
    r"__declspec\s*\(\s*thread\s*\)")

# Heuristic declaration patterns for the advisory layer.  Keep these mutually
# exclusive: a line matching two patterns would be reported twice.
DECL_PATTERNS = (
    (_TLS_STORAGE, "thread-local"),
)

# Headers scanned for mutable state that has *internal linkage*, which gives
# every including translation unit its own copy.  ``llvm/include`` is in scope
# because this tree is a header-only port of LLVM: state that upstream keeps in
# a .cpp now sits in headers the plugin path includes, so a split there is
# plugin-visible process state.  See ``scan_headers``.
HEADER_AUDIT_DIRS = (
    "llvm/include",
    "neverc/include/neverc/Foundation/Core/LLVMTimeTraceRootLease.h",
    "neverc/include/neverc/Plugin",
    "neverc/include/neverc/DynCode",
    "neverc/include/neverc/Linker",
)


@dataclass
class Report:
    forbidden: list[str] = field(default_factory=list)
    heuristic: list[str] = field(default_factory=list)
    headers: list[str] = field(default_factory=list)
    source_globals: list[str] = field(default_factory=list)
    binary: list[str] = field(default_factory=list)
    # Reasons a configured source/header/binary scan could not run. Kept apart
    # from findings so "the input was unavailable" never reads as "clean".
    unscannable: list[str] = field(default_factory=list)

    def failed(self, strict: bool) -> bool:
        if (self.forbidden or self.headers or self.source_globals or
                self.binary or self.unscannable):
            return True
        if strict and self.heuristic:
            return True
        return False


def is_digit_separator(text: str, i: int) -> bool:
    """Report whether ``text[i]`` is a C++14 digit separator, not a quote.

    ``0x3b00'0000`` is one number, but reading that apostrophe as the start of a
    character literal makes the scanner run to the *next* quote and drop
    everything in between -- which can hide a forbidden symbol from the gate
    entirely.  A separator always sits between digits of a literal, whereas an
    encoding prefix (``u8'x'``, ``L'x'``) leaves a non-digit at the head of the
    token.
    """
    if i == 0 or i + 1 >= len(text):
        return False
    if not text[i + 1].isalnum():
        return False
    j = i - 1
    while j >= 0 and (text[j].isalnum() or text[j] in "'."):
        j -= 1
    head = text[j + 1:i]
    return bool(head) and head[0].isdigit()


# A raw string opener, optionally encoding-prefixed: R"delim( ... )delim".  The
# delimiter excludes whitespace, parentheses and backslash, per [lex.string].
_RAW_STRING_OPEN = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\v\f\n]{0,16})\(')


def raw_string_end(text: str, i: int) -> int | None:
    """Return the index just past the raw string starting at *i*, else None.

    Raw strings ignore escapes and may embed quotes, so the ordinary literal
    scanner would end one early and then read its contents as code.
    """
    match = _RAW_STRING_OPEN.match(text, i)
    if not match:
        return None
    closer = ")" + match.group(1) + '"'
    end = text.find(closer, match.end())
    return len(text) if end == -1 else end + len(closer)


def blank_preserving_newlines(fragment: str) -> str:
    """Replace tokens with whitespace without changing source offsets."""
    return "".join("\n" if char == "\n" else " " for char in fragment)


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
                out.append(" " * (n - i))
                break
            out.append(" " * (j - i))
            i = j
            continue
        if two == "/*":
            start = i
            j = text.find("*/", i + 2)
            end = n if j == -1 else j + 2
            # A comment is whitespace in C++. Keep both token separation and
            # every newline so matching and diagnostic locations agree with
            # the compiler's token stream.
            out.append(blank_preserving_newlines(text[start:end]))
            i = end
            continue
        # A raw string may open on its encoding prefix, so only consider one
        # when the preceding character cannot be part of an identifier.
        if c in "RLuU" and (i == 0 or not (text[i - 1].isalnum()
                                           or text[i - 1] == "_")):
            end = raw_string_end(text, i)
            if end is not None:
                out.append(blank_preserving_newlines(text[i:end]))
                i = end
                continue
        if c == "'" and is_digit_separator(text, i):
            out.append(c)
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
            start = i
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            masked = list(blank_preserving_newlines(text[start:i]))
            masked[0] = quote
            if i > start + 1 and text[i - 1] == quote:
                masked[-1] = quote
            out.append("".join(masked))
            continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_comments_preserve_literals(text: str) -> str:
    """Remove comments while retaining complete literal token spellings."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            end = text.find("\n", i)
            if end == -1:
                out.append(" " * (n - i))
                break
            out.append(" " * (end - i))
            i = end
            continue
        if two == "/*":
            end_marker = text.find("*/", i + 2)
            end = n if end_marker == -1 else end_marker + 2
            out.append(blank_preserving_newlines(text[i:end]))
            i = end
            continue
        c = text[i]
        if c in "RLuU" and (i == 0 or not (text[i - 1].isalnum()
                                           or text[i - 1] == "_")):
            end = raw_string_end(text, i)
            if end is not None:
                out.append(text[i:end])
                i = end
                continue
        if c == "'" and is_digit_separator(text, i):
            out.append(c)
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
            start = i
            i += 1
            while i < n:
                if text[i] == "\\":
                    i = min(i + 2, n)
                    continue
                i += 1
                if text[i - 1] == quote:
                    break
            out.append(text[start:i])
            continue
        out.append(c)
        i += 1
    return "".join(out)


def normalize_cpp_whitespace_preserve_literals(text: str) -> str:
    """Fold trivia whitespace while preserving every literal byte."""
    out: list[str] = []
    pending_space = False
    i = 0
    n = len(text)
    while i < n:
        if text[i].isspace():
            pending_space = bool(out)
            i += 1
            continue
        if pending_space:
            out.append(" ")
            pending_space = False
        c = text[i]
        if c in "RLuU" and (i == 0 or not (text[i - 1].isalnum()
                                           or text[i - 1] == "_")):
            end = raw_string_end(text, i)
            if end is not None:
                out.append(text[i:end])
                i = end
                continue
        if c == "'" and is_digit_separator(text, i):
            out.append(c)
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
            start = i
            i += 1
            while i < n:
                if text[i] == "\\":
                    i = min(i + 2, n)
                    continue
                i += 1
                if text[i - 1] == quote:
                    break
            out.append(text[start:i])
            continue
        out.append(c)
        i += 1
    return "".join(out).rstrip()


def iter_source_files(paths: Iterable[pathlib.Path]):
    for base in paths:
        if base.is_file():
            yield base
            continue
        if not base.exists():
            continue
        for suffix in ("*.h", "*.hpp", "*.c", "*.cc", "*.cpp", "*.inc"):
            yield from base.rglob(suffix)


def splice_line_continuations(text: str) -> str:
    """Apply C/C++ phase-2 line splicing before lexical auditing.

    Without this, ``thread_\\\nlocal`` and even a forbidden identifier can be
    split in the physical file while the compiler sees one token. Diagnostics
    after a splice use logical line numbers; the gate favours seeing the token
    over preserving a cosmetically exact physical line in this rare form.
    """
    return re.sub(r"\\(?:\r\n|\n|\r)", "", text)


def report_missing_audit_paths(report: Report,
                               paths: Iterable[pathlib.Path],
                               layer: str) -> None:
    """Make a stale configured audit root a gate failure, not an empty scan."""
    for path in paths:
        if path.exists():
            continue
        try:
            display = path.relative_to(ROOT).as_posix()
        except ValueError:
            display = str(path)
        finding = f"configured {layer} audit path is missing: {display}"
        if finding not in report.unscannable:
            report.unscannable.append(finding)


def load_allowlist() -> dict:
    if not ALLOWLIST_PATH.exists():
        return {"entries": []}
    return json.loads(ALLOWLIST_PATH.read_text(encoding="utf-8"))


def relative_key(path: pathlib.Path) -> str:
    """Path relative to ROOT, always '/'-separated.

    The allowlist spells its paths with forward slashes, so a Windows run that
    compared against ``neverc\\lib\\...`` matched nothing and reported every
    documented thread_local as a violation -- a gate that passed on Linux and
    macOS and failed on Windows for identical sources.
    """
    return path.relative_to(ROOT).as_posix()


def single_declared_identifier(snippet: str) -> str | None:
    """Return the sole simple TLS declarator, or fail closed.

    An allowlisted name must be the object being declared, not a token in its
    type or initializer.  This deliberately small declaration scanner accepts
    the forms used by the audited facades, including template types and
    call/braced initializers, while rejecting multiple declarators and syntax
    it cannot classify safely.  It is not intended to parse arbitrary C++.
    """
    storage = _TLS_STORAGE.search(snippet)
    if not storage:
        return None

    text = snippet[storage.end():]
    identifiers: list[str] = []
    paren_depth = 0
    square_depth = 0
    brace_depth = 0
    angle_depth = 0
    in_initializer = False
    terminated = False
    i = 0

    def at_declaration_level() -> bool:
        return paren_depth == square_depth == brace_depth == 0

    while i < len(text):
        c = text[i]
        if terminated:
            if not c.isspace():
                return None
            i += 1
            continue

        if c.isalpha() or c == "_":
            j = i + 1
            while j < len(text) and (text[j].isalnum() or text[j] == "_"):
                j += 1
            if (not in_initializer and at_declaration_level() and
                    angle_depth == 0):
                identifiers.append(text[i:j])
            i = j
            continue

        if c == "(":
            # With at least a type and a candidate object name, a top-level
            # parenthesis starts direct initialization.  Parentheses earlier
            # in the decl-specifier sequence (for example decltype/alignas)
            # remain part of that sequence.
            if (not in_initializer and at_declaration_level() and
                    angle_depth == 0 and len(identifiers) >= 2):
                in_initializer = True
            paren_depth += 1
        elif c == ")":
            if paren_depth == 0:
                return None
            paren_depth -= 1
        elif c == "[":
            square_depth += 1
        elif c == "]":
            if square_depth == 0:
                return None
            square_depth -= 1
        elif c == "{":
            if (not in_initializer and at_declaration_level() and
                    angle_depth == 0):
                in_initializer = True
            brace_depth += 1
        elif c == "}":
            if brace_depth == 0:
                return None
            brace_depth -= 1
        elif c == "<" and not in_initializer and paren_depth == 0:
            angle_depth += 1
        elif c == ">" and not in_initializer and paren_depth == 0:
            if angle_depth == 0:
                return None
            angle_depth -= 1
        elif c == "=" and not in_initializer and at_declaration_level():
            if angle_depth == 0:
                in_initializer = True
        elif c == "," and at_declaration_level():
            # Once an initializer starts, angle brackets may be comparison
            # operators rather than templates.  Treat every ungrouped comma
            # there as another declarator; false positives fail the gate.
            if in_initializer or angle_depth == 0:
                return None
        elif c == ";" and at_declaration_level() and angle_depth == 0:
            terminated = True
        i += 1

    if (not terminated or paren_depth != 0 or square_depth != 0 or
            brace_depth != 0 or angle_depth != 0 or not identifiers):
        return None
    return identifiers[-1]


def allowlisted(rel: str, snippet: str, allowlist: dict) -> bool:
    rel = rel.replace("\\", "/")
    declaration = canonical_object_declaration(snippet)
    if declaration is None:
        return False
    for entry in allowlist.get("entries", []):
        files = [path.replace("\\", "/")
                 for path in entry.get("files", [])]
        if rel not in files:
            continue
        declared = single_declared_identifier(snippet)
        if declared is None:
            return False
        symbol = entry.get("symbol")
        if (symbol and declared == symbol and
                entry.get("declaration") == declaration and
                entry.get("definition_sha256") ==
                object_definition_sha256(snippet)):
            return True
    return False


def canonical_object_declaration(statement: str) -> str | None:
    """Return the normalized declarator used by exact review manifests."""
    text = " ".join(statement.split()).rstrip(";").rstrip()
    # scope_scan surfaces a braced definition at its opening brace; direct
    # callers may provide the full ``Name{...}`` spelling. In both forms the
    # part before the top-level brace is the object declarator.
    paren = square = 0
    for i, char in enumerate(text):
        if char == "(":
            paren += 1
        elif char == ")":
            paren = max(paren - 1, 0)
        elif char == "[":
            square += 1
        elif char == "]":
            square = max(square - 1, 0)
        elif char == "{" and paren == square == 0:
            text = text[:i].rstrip()
            break
    declarator = object_declarator(text)
    return " ".join(declarator.split()) if declarator is not None else None


def canonical_object_definition(statement: str) -> str:
    """Return a stable structural spelling of a complete object definition.

    Review manifests bind both the declarator and the complete initializer,
    including string/character/raw-literal contents. Comments are not semantic
    and are erased before whitespace normalization.
    """
    code = strip_comments_preserve_literals(splice_line_continuations(
        statement))
    return normalize_cpp_whitespace_preserve_literals(code).rstrip(
        ";").rstrip()


def object_definition_sha256(statement: str) -> str:
    canonical = canonical_object_definition(statement)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def declared_object_identifier(statement: str) -> str | None:
    """Return the simple identifier of an object declaration we understand."""
    declarator = canonical_object_declaration(statement)
    if declarator is None:
        return None
    tail = _ENDS_IN_IDENTIFIER.search(declarator)
    return tail.group("name") if tail else None


def header_constant_allowlisted(rel: str, statement: str,
                                allowlist: dict) -> bool:
    """Match one complex constexpr header object by exact path and identifier.

    A complex ``constexpr`` object is not automatically immutable all the way
    down: records may contain ``mutable`` subobjects, and pointers expose a
    separate object. Each reviewed exception therefore needs an exact manifest
    entry with an owner and a concrete justification.
    """
    rel = rel.replace("\\", "/")
    declaration = canonical_object_declaration(statement)
    symbol = declared_object_identifier(statement)
    if symbol is None or declaration is None:
        return False
    for entry in allowlist.get("header_constants", []):
        if entry.get("file", "").replace("\\", "/") != rel:
            continue
        if entry.get("symbol") != symbol:
            continue
        if entry.get("declaration") != declaration:
            continue
        if entry.get("definition_sha256") != object_definition_sha256(
                statement):
            continue
        if not all(isinstance(entry.get(field), str) and
                   bool(entry[field].strip())
                   for field in ("declaration", "definition_sha256", "owner",
                                 "justification")):
            return False
        return True
    return False


def source_object_allowlisted(rel: str, statement: str,
                              allowlist: dict) -> bool:
    """Match one path-provenanced source object by exact identifier."""
    rel = rel.replace("\\", "/")
    declaration = canonical_object_declaration(statement)
    symbol = declared_object_identifier(statement)
    if symbol is None or declaration is None:
        return False
    for entry in allowlist.get("source_objects", []):
        if entry.get("file", "").replace("\\", "/") != rel:
            continue
        if entry.get("symbol") != symbol:
            continue
        if entry.get("declaration") != declaration:
            continue
        if entry.get("definition_sha256") != object_definition_sha256(
                statement):
            continue
        if not all(isinstance(entry.get(field), str) and
                   bool(entry[field].strip())
                   for field in ("declaration", "definition_sha256", "owner", "kind",
                                 "justification")):
            return False
        return True
    return False


def binary_source_object_allowlisted(rel: str, statement: str,
                                     allowlist: dict) -> bool:
    """Match the source definition promised by one binary exception.

    A demangled symbol name is not sufficient provenance for a security
    exception: an unrelated writable object can be renamed to the marker, and
    an initializer can acquire state without changing that marker.  Bind every
    binary entry to exactly one complete source definition as well as requiring
    exactly one linked occurrence.
    """
    rel = rel.replace("\\", "/")
    declaration = canonical_object_declaration(statement)
    symbol = declared_object_identifier(statement)
    if symbol is None or declaration is None:
        return False
    for entry in allowlist.get("binary_symbols", []):
        if entry.get("file", "").replace("\\", "/") != rel:
            continue
        if entry.get("source_symbol") != symbol:
            continue
        if entry.get("declaration") != declaration:
            continue
        if entry.get("definition_sha256") != object_definition_sha256(
                statement):
            continue
        return True
    return False


def is_canonical_promotion_suffix(suffix: str) -> bool:
    if not re.fullmatch(r"(?:\.llvm\.[0-9]+)+", suffix):
        return False
    for digits in re.findall(r"\.llvm\.([0-9]+)", suffix):
        if ((len(digits) > 1 and digits.startswith("0")) or
                int(digits) > 0xffffffffffffffff):
            return False
    return True


def matches_llvm_promoted_symbol(name: str, marker: str) -> bool:
    if name == marker:
        return True
    if not name.startswith(marker):
        return False
    suffix = name[len(marker):]
    if suffix.startswith(" (") and suffix.endswith(")"):
        suffix = suffix[2:-1]
    elif suffix.startswith(" [clone ") and suffix.endswith("]"):
        suffix = suffix[8:-1]
    return is_canonical_promotion_suffix(suffix)


def binary_allowlist_entry(name: str, allowlist: dict) -> dict | None:
    """Return the exact binary-symbol manifest entry matching *name*."""
    for entry in allowlist.get("binary_symbols", []):
        marker = entry.get("symbol")
        if marker and matches_llvm_promoted_symbol(name, marker):
            return entry
    return None


def allowlisted_binary_symbol(name: str, allowlist: dict) -> bool:
    """Match a writable symbol against the ``binary_symbols`` list.

    Only address-only pass/type identity tokens, const objects whose vtable
    pointer forces a relocation, and sync primitives without compilation state
    are eligible; each entry carries an owner and justification.  A lookup or
    interface table is not: making it ``constexpr`` moves it to read-only
    storage, which is a fix rather than a documented exception.  Thread-local
    symbols never reach here -- see ``symbol_is_thread_local``.
    """
    return binary_allowlist_entry(name, allowlist) is not None


def source_audit_bases() -> list[pathlib.Path]:
    return ([ROOT / directory for directory in AUDIT_DIRS] +
            [ROOT / filename for filename in EXTRA_AUDIT_FILES])


def audited_source_files() -> dict[str, pathlib.Path]:
    """Return the exact source dependency closure covered by this gate."""
    return {relative_key(path): path
            for path in iter_source_files(source_audit_bases())}


def iter_tls_declarations(code: str, semantic_code: str | None = None):
    """Yield ``(match, kind, declaration)`` for supported TLS spellings."""
    if semantic_code is None:
        semantic_code = code
    if len(semantic_code) != len(code):
        raise ValueError("TLS structural/semantic source views are misaligned")
    for pattern, name in DECL_PATTERNS:
        for match in pattern.finditer(code):
            start = max(code.rfind(delimiter, 0, match.start())
                        for delimiter in ";{}") + 1
            end = code.find(";", match.end())
            snippet = semantic_code[
                start:end + 1 if end >= 0 else len(code)].strip()
            yield match, name, snippet


def scan_sources(report: Report, allowlist: dict) -> None:
    bases = source_audit_bases()
    report_missing_audit_paths(report, bases, "source")
    for path in iter_source_files(bases):
        rel = relative_key(path)
        raw = path.read_text(encoding="utf-8", errors="replace")
        spliced = splice_line_continuations(raw)
        code = strip_comments_and_strings(spliced)
        semantic_code = strip_comments_preserve_literals(spliced)

        # Zero-tolerance checks operate on the complete stripped translation
        # unit. C++ permits whitespace (including newlines) around ``::`` and
        # throughout declarations, so a physical-line scan is not a hard gate.
        for pattern, token in FORBIDDEN_SYMBOL_PATTERNS:
            for match in pattern.finditer(code):
                line = code.count("\n", 0, match.start()) + 1
                report.forbidden.append(
                    f"{rel}:{line}: forbidden `{token}`")
        for pattern, name in FORBIDDEN_REGEXES:
            for match in pattern.finditer(code):
                line = code.count("\n", 0, match.start()) + 1
                report.forbidden.append(
                    f"{rel}:{line}: forbidden `{name}`")

        # Match the complete translation unit. ``__declspec(thread)`` permits
        # newlines inside its parentheses, and line-by-line matching silently
        # missed that legal spelling. Extract the containing declaration for
        # exact allowlist parsing; an unfamiliar declaration fails closed.
        for match, name, snippet in iter_tls_declarations(
                code, semantic_code):
            if allowlisted(rel, snippet, allowlist):
                continue
            line = code.count("\n", 0, match.start()) + 1
            report.heuristic.append(
                f"{rel}:{line}: {name} storage `{snippet[:80]}`")


_OPEN_NAMESPACE = re.compile(r"\bnamespace\b(?P<name>[\w\s:]*)$")
_OPEN_LINKAGE = re.compile(
    r'^\s*extern\s+(?:"\s*"|"C"|"C\+\+")\s*$')
_OPEN_RECORD = re.compile(r"\b(?:struct|class|union|enum)\b[^;=]*$")
_HAS_STATIC = re.compile(r"(?:^|\s)static(?:\s|$)")
_ENDS_IN_IDENTIFIER = re.compile(
    r"(?P<name>[A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*$")
_NOT_AN_OBJECT_STATEMENT = re.compile(
    r"^\s*(?:using|typedef|friend|return|static_assert|namespace)\b")
# Types defined in an anonymous namespace, which is what makes a variable of
# that type internally linked no matter how the variable itself is spelled.
_ANON_TYPE = re.compile(r"\b(?:struct|class|union|enum)\s+(\w+)\b")

# A pointer-to-function object hides its identifier inside a top-level
# parenthesised declarator: ``void (CALL *Hook[2])(int)``. Keep the grammar
# deliberately narrow so a function parameter or ``(*factory())(int)`` is not
# promoted into an object finding.
_FUNCTION_POINTER_GROUP = re.compile(
    r"\s*(?:[A-Za-z_]\w*\s+)*"
    r"(?:(?:[A-Za-z_]\w*)\s*::\s*)*\*\s*"
    r"(?:(?:const|volatile|restrict)\s+)*"
    r"(?P<name>[A-Za-z_]\w*)\s*"
    r"(?:\[[^\]]*\]\s*)*")

_READ_ONLY_BUILTINS = {
    "bool", "char", "signed char", "unsigned char", "wchar_t", "char8_t",
    "char16_t", "char32_t", "short", "short int", "signed short",
    "signed short int", "unsigned short", "unsigned short int", "int",
    "signed", "signed int", "unsigned", "unsigned int", "long",
    "long int", "signed long", "signed long int", "unsigned long",
    "unsigned long int", "long long", "long long int", "signed long long",
    "signed long long int", "unsigned long long", "unsigned long long int",
    "float", "double", "long double", "std::byte",
}
_READ_ONLY_FIXED_WIDTH = re.compile(
    r"(?:std\s*::\s*)?(?:u?int(?:8|16|32|64)_t|u?intptr_t|size_t|ptrdiff_t)")


def top_level_initializer_equal(text: str) -> int | None:
    """Return the top-level copy-initialiser ``=``, if one exists."""
    paren = square = brace = 0
    for i, char in enumerate(text):
        if char == "(":
            paren += 1
        elif char == ")":
            paren = max(paren - 1, 0)
        elif char == "[":
            square += 1
        elif char == "]":
            square = max(square - 1, 0)
        elif char == "{":
            brace += 1
        elif char == "}":
            brace = max(brace - 1, 0)
        elif char == "=" and paren == square == brace == 0:
            # ``operator=(...)`` names a function; this token is not a
            # copy-initializer. A later ``= default``/``= delete`` is still
            # visible to the normal function-declaration grammar.
            if text[:i].rstrip().endswith("operator"):
                continue
            before = text[i - 1] if i else ""
            after = text[i + 1] if i + 1 < len(text) else ""
            if ((not before or before not in "!<>=") and
                    (not after or after not in "=>")):
                return i
    return None


def top_level_parentheses(text: str):
    """Yield the ranges/content of balanced top-level parenthesis groups."""
    depth = 0
    start = None
    for i, char in enumerate(text):
        if char == "(":
            if depth == 0:
                start = i
            depth += 1
        elif char == ")" and depth:
            depth -= 1
            if depth == 0 and start is not None:
                yield start, i + 1, text[start + 1:i]
                start = None


def strip_template_heads(statement: str) -> str | None:
    """Remove one or more balanced C++ template-heads.

    Variable templates are definitions just like ordinary namespace variables;
    treating every statement beginning with ``template`` as a declaration-only
    form left a legal way around the header-state gate. An unbalanced head is
    unknown syntax and therefore fails closed at the caller.
    """
    text = statement.lstrip()
    while re.match(r"template\b", text):
        match = re.match(r"template\s*<", text)
        if not match:
            return None
        depth = 1
        i = match.end()
        while i < len(text) and depth:
            if text[i] == "<":
                depth += 1
            elif text[i] == ">":
                depth -= 1
            i += 1
        if depth:
            return None
        text = text[i:].lstrip()
    return text


_CPP_ATTRIBUTE_SUFFIX = re.compile(r"\s*\[\[.*\]\]\s*$")
_GNU_ATTRIBUTE_SUFFIX = re.compile(
    r"\s*__attribute__\s*\(\(.*\)\)\s*$")
_ASM_LABEL_SUFFIX = re.compile(
    r"\s+(?:asm|__asm__)\s*\(\s*(?:\"\"|''|[^()]*)\s*\)\s*$")


def strip_post_declarator_attributes(declarator: str) -> str:
    """Strip legal attributes that follow an object's identifier.

    The input has already had strings/comments removed and whitespace folded.
    Repeating handles combinations such as ``x [[maybe_unused]]
    __attribute__((used))`` without weakening the identifier match itself.
    """
    previous = None
    while declarator != previous:
        previous = declarator
        declarator = _CPP_ATTRIBUTE_SUFFIX.sub("", declarator)
        declarator = _GNU_ATTRIBUTE_SUFFIX.sub("", declarator)
    return declarator.strip()


def strip_asm_label(declarator: str) -> str:
    """Remove a GNU/MS-compatible object asm-label suffix.

    The label controls the emitted symbol spelling but does not turn an object
    definition into a function declaration. Missing it is especially unsafe:
    the renamed symbol also loses the owner namespace used by the artifact
    audit.
    """
    return _ASM_LABEL_SUFFIX.sub("", declarator).strip()


def direct_initializer_starts_with_expression(content: str) -> bool:
    """Recognise direct initializers that cannot be parameter declarations."""
    return re.match(
        r"\s*(?:[-+]?(?:\d|\.\d)|\"|'|true\b|false\b|nullptr\b|\{)",
        content) is not None


def parenthesized_object_name(prefix: str, content: str) -> str | None:
    """Recover redundant parentheses around an object's declared name.

    ``int (State)`` is an object, while ``Registry State(Arg)`` remains a
    potentially most-vexing function declaration. We accept the former only
    when the prefix is a type-only spelling rather than an existing declarator.
    """
    name = content.strip()
    if re.fullmatch(r"[A-Za-z_]\w*", name) is None:
        return None
    reduced = re.sub(
        r"\b(?:alignas|consteval|constexpr|constinit|extern|inline|register|"
        r"static|thread_local|typedef|volatile)\b", " ", prefix)
    reduced = " ".join(reduced.split())
    if not reduced:
        return None
    builtin_words = set(_READ_ONLY_BUILTINS) | {
        "auto", "signed", "unsigned", "short", "long", "const", "volatile",
    }
    words = re.findall(r"[A-Za-z_]\w*", reduced)
    type_only = (
        len(words) == 1 or
        all(word in builtin_words for word in words) or
        reduced.startswith(("struct ", "class ", "union ", "enum ")) or
        reduced.endswith((">", "*", "&")) or "::" in reduced)
    if not type_only:
        return None
    return f"{prefix.rstrip()} {name}"


def has_qualified_declared_name(declarator: str) -> bool:
    """Report whether the declared object's owner is explicitly qualified."""
    tail = _ENDS_IN_IDENTIFIER.search(declarator)
    return bool(tail and declarator[:tail.start()].rstrip().endswith("::"))


def qualified_declared_owner(declarator: str) -> str:
    """Return the explicit owner of a qualified object definition."""
    tail = _ENDS_IN_IDENTIFIER.search(declarator)
    if tail is None:
        return ""
    prefix = declarator[:tail.start()].rstrip()
    if not prefix.endswith("::"):
        return ""
    match = re.search(
        r"(?P<owner>[A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*::$",
        prefix)
    if match is None:
        return ""
    return re.sub(r"\s*::\s*", "::", match.group("owner"))


def has_qualified_function_owner(head: str) -> bool:
    """Recognise an out-of-namespace qualified function definition head."""
    groups = list(top_level_parentheses(head))
    if not groups:
        return False
    before = head[:groups[0][0]].rstrip()
    return re.search(
        r"(?:[A-Za-z_]\w*\s*::\s*)+[~A-Za-z_]\w*$", before) is not None


def object_declarator(statement: str) -> str | None:
    """Return an object declarator, or ``None`` for a function/unknown form.

    Copy initialisation is decisive even when its initializer calls a
    function. A narrow set of unambiguous direct initializers is also
    recognised; remaining parenthesised declarations are unknown and stay
    subject to the artifact layer until Clang-AST provenance replaces this
    lexical parser.
    """
    statement = strip_template_heads(statement)
    if statement is None:
        return None
    if not re.match(r"(?:\[\[|::|[A-Za-z_])", statement.lstrip()):
        return None
    paren = square = 0
    for index, char in enumerate(statement):
        if char == "(":
            paren += 1
        elif char == ")":
            paren = max(paren - 1, 0)
        elif char == "[":
            square += 1
        elif char == "]":
            square = max(square - 1, 0)
        elif char == "{" and paren == square == 0:
            statement = statement[:index].rstrip()
            break
    initializer = top_level_initializer_equal(statement)
    declarator = (statement[:initializer] if initializer is not None
                  else statement).strip()
    declarator = strip_post_declarator_attributes(declarator)
    declarator = strip_asm_label(declarator)

    for start, end, content in top_level_parentheses(declarator):
        match = _FUNCTION_POINTER_GROUP.fullmatch(content)
        if match and declarator[end:].lstrip().startswith("("):
            return declarator

    if initializer is not None:
        suffix = statement[initializer + 1:].strip().rstrip(";").strip()
        if (suffix in ("default", "delete", "0") and
                list(top_level_parentheses(declarator))):
            return None

    if initializer is None:
        groups = list(top_level_parentheses(declarator))
        if groups:
            start, end, content = groups[-1]
            direct_initializer = (
                len(groups) == 1 and
                end == len(declarator) and
                direct_initializer_starts_with_expression(content))
            if direct_initializer:
                prefix = declarator[:start].rstrip()
                if _ENDS_IN_IDENTIFIER.search(prefix):
                    return prefix
            if len(groups) == 1 and end == len(declarator):
                parenthesized = parenthesized_object_name(
                    declarator[:start], content)
                if parenthesized is not None:
                    return parenthesized
            suffix = declarator[groups[-1][1]:].strip()
            if (not suffix or suffix.startswith("->") or
                    re.fullmatch(
                        r"(?:(?:const|volatile|noexcept|override|final)\s*)+",
                        suffix) or
                    re.fullmatch(
                        r"(?:[A-Z_][A-Z0-9_]*(?:\([^)]*\))?\s*)+",
                        suffix)):
                return None
    tail = _ENDS_IN_IDENTIFIER.search(declarator)
    if not tail:
        return None
    return declarator


def is_non_object_statement(statement: str) -> bool:
    """Reject declarations that are certainly not object definitions."""
    if _NOT_AN_OBJECT_STATEMENT.match(statement):
        return True
    if re.match(r"^\s*extern\b", statement):
        if top_level_initializer_equal(statement) is not None or "{" in statement:
            return False
        groups = list(top_level_parentheses(statement))
        return not (
            len(groups) == 1 and groups[0][1] == len(statement) and
            direct_initializer_starts_with_expression(groups[0][2]))
    # An elaborated type can either be a forward declaration (`struct S`) or
    # the type spelling of an object (`struct S Value`). Only the former is
    # certainly not storage.
    if re.fullmatch(
            r"\s*(?:struct|class|union)\s+"
            r"(?:(?:[A-Za-z_]\w*)\s*::\s*)*[A-Za-z_]\w*\s*",
            statement):
        return True
    if re.fullmatch(
            r"\s*enum(?:\s+class)?\s+"
            r"(?:(?:[A-Za-z_]\w*)\s*::\s*)*[A-Za-z_]\w*"
            r"(?:\s*:\s*[^=]+)?\s*", statement):
        return True
    candidate = strip_template_heads(statement)
    if candidate is not None:
        # scope_scan surfaces a type-definition head at its opening brace.
        # Keep elaborated object spellings such as ``struct State Rogue``: the
        # second identifier prevents these definition-only forms matching.
        if re.fullmatch(
                r"\s*(?:struct|class|union)\s+"
                r"(?:(?:[A-Za-z_]\w*)\s*::\s*)*[A-Za-z_]\w*"
                r"(?:\s+final)?(?:\s*:\s*.+)?\s*",
                candidate):
            return True
        if re.fullmatch(r"\s*enum\s*:\s*.+", candidate):
            return True
    return False


def is_read_only(declarator: str) -> bool:
    """Report whether the declared object itself cannot be written.

    This is a positive proof, not a shallow ``const`` search. ``constexpr`` is
    language-level immutable. Otherwise only const builtin scalar/array data
    is exempt. Pointers, references, records, templates and inferred types can
    own or expose mutable state even when the outer object is const.
    """
    if "*" in declarator or "&" in declarator:
        return False
    if not re.search(r"\b(?:const|constexpr)\b", declarator):
        return False
    tail = _ENDS_IN_IDENTIFIER.search(declarator)
    if not tail:
        return False
    type_part = declarator[:tail.start()]
    type_part = re.sub(r"\b(?:const|constexpr|inline|static|volatile)\b", " ",
                       type_part)
    normalized = " ".join(type_part.split())
    normalized = re.sub(r"\s*::\s*", "::", normalized)
    return (normalized in _READ_ONLY_BUILTINS or
            _READ_ONLY_FIXED_WIDTH.fullmatch(normalized) is not None)


def strip_preprocessor(text: str) -> str:
    """Blank out directive lines, keeping line numbers.

    Without this a declaration absorbs the ``#endif`` sitting above it and the
    statement no longer parses as one.
    """
    out: list[str] = []
    continued = False
    for line in text.splitlines(True):
        body = line.rstrip("\n")
        if continued or body.lstrip().startswith("#"):
            continued = body.rstrip().endswith("\\")
            out.append("\n" if line.endswith("\n") else "")
        else:
            out.append(line)
    return "".join(out)


def brace_starts_object_initializer(head: str) -> bool:
    """Distinguish an object/list initializer from a new C++ scope."""
    if (not head or _OPEN_NAMESPACE.search(head) or
            _OPEN_LINKAGE.fullmatch(head)):
        return False
    if re.match(r"^(?:if|for|while|switch|catch|try|else|do)\b", head):
        return False
    if (_OPEN_RECORD.search(head) and
            is_non_object_statement(head)):
        return False
    if top_level_initializer_equal(head) is not None:
        return True
    return object_declarator(head) is not None


def namespace_scope(name: str) -> str:
    normalized = re.sub(r"\s*::\s*", "::", " ".join(name.split()))
    return f"ns:{normalized}"


def scope_kind(scope: str) -> str:
    return "ns" if scope.startswith("ns:") else scope


def named_namespace_owner(scopes: Iterable[str]) -> str:
    parts: list[str] = []
    for scope in scopes:
        if not scope.startswith("ns:"):
            continue
        name = scope[3:]
        if name:
            parts.extend(component for component in name.split("::")
                         if component)
    return "::".join(parts)


def binary_owner_covers(owner: str) -> bool:
    return any(owner == scope or owner.startswith(scope + "::")
               for scope in _BINARY_SCOPES)


def scope_scan(text: str):
    """Yield ``(offset, scopes, statement)`` for each ``;``-terminated statement.

    ``scopes`` is the stack of enclosing blocks: ``anon`` for an anonymous
    namespace, ``ns`` for a named one, ``record`` for a class/struct/union,
    ``block-internal`` for a function the linker cannot merge across
    translation units, and ``block`` for any other body.
    """
    scopes: list[str] = []
    stmt_start = 0
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "{":
            head = text[stmt_start:i].strip()
            if brace_starts_object_initializer(head):
                # Keep the complete initializer in the statement so exception
                # manifests bind its semantics. Initializer braces, including
                # lambda/aggregate nesting, do not push a C++ lexical scope.
                depth = 1
                i += 1
                while i < n and depth:
                    if text[i] == "{":
                        depth += 1
                    elif text[i] == "}":
                        depth -= 1
                    i += 1
                continue
            if head:
                # Surface the head too: a type definition is terminated by the
                # brace, not by a ';' at this level, so a caller looking for
                # `struct X` would otherwise never see it.
                yield stmt_start, list(scopes), text[stmt_start:i]
            match = _OPEN_NAMESPACE.search(head)
            if match:
                name = match.group("name").strip()
                scopes.append(namespace_scope(name) if name else "anon")
            elif _OPEN_LINKAGE.fullmatch(head):
                # A language-linkage block is namespace-like for storage and
                # linkage purposes; it is not an ordinary function/body block.
                scopes.append(namespace_scope(""))
            elif _OPEN_RECORD.search(head):
                scopes.append("record")
            elif ("block-qualified" in scopes or
                  has_qualified_function_owner(head)):
                scopes.append("block-qualified")
            elif ("block-internal" in scopes or "anon" in scopes or
                  (_HAS_STATIC.search(head) and scopes[-1:] != ["record"])):
                # Inside a class, `static` names a static member function,
                # which keeps the linkage of its class -- not the same thing.
                scopes.append("block-internal")
            else:
                scopes.append("block")
            stmt_start = i + 1
        elif c == "}":
            if scopes:
                scopes.pop()
            stmt_start = i + 1
        elif c == ";":
            yield stmt_start, list(scopes), text[stmt_start:i]
            stmt_start = i + 1
        elif c == "(":
            # Step over parenthesised text: it can hold ';' (a for-statement)
            # and '{' (a braced initializer or lambda) that open no scope here.
            depth = 1
            i += 1
            while i < n and depth:
                if text[i] == "(":
                    depth += 1
                elif text[i] == ")":
                    depth -= 1
                i += 1
            continue
        i += 1


def header_internal_state(text: str):
    """Yield ``(line, kind, statement)`` for mutable objects with internal
    linkage defined in a header.

    Such an object is one per *including translation unit*, not one per
    program: a flag set through one includer is unset for the next, and a
    registry filled by one is empty for another.  Upstream LLVM is safe from
    this because the definitions live in .cpp files; this port moved them into
    headers, where `static` and anonymous namespaces mean the opposite of what
    they meant before.
    """
    spliced = splice_line_continuations(text)
    code = strip_preprocessor(strip_comments_and_strings(spliced))
    semantic_code = strip_preprocessor(strip_comments_preserve_literals(
        spliced))
    if len(semantic_code) != len(code):
        raise ValueError("header structural/semantic source views misaligned")
    # A variable whose type lives in an anonymous namespace is internally
    # linked whatever the variable's own spelling says, so `inline` on it is a
    # promise the language cannot keep.  Collecting the type names first is
    # what lets the statement scan below recognise that case.
    anon_types: set[str] = set()
    for _, scopes, stmt in scope_scan(code):
        if "anon" in scopes:
            anon_types.update(_ANON_TYPE.findall(stmt))
    for offset, scopes, stmt in scope_scan(code):
        flat = " ".join(stmt.split())
        candidate = strip_template_heads(flat) if flat else None
        if (not candidate or
                (len(candidate.split()) < 2 and
                 "*" not in candidate and "&" not in candidate) or
                is_non_object_statement(candidate)):
            continue
        head = object_declarator(candidate)
        if head is None:
            continue
        innermost = scope_kind(scopes[-1]) if scopes else "ns"
        if innermost == "record":
            if "anon" not in scopes or not _HAS_STATIC.search(head):
                continue                # member of a program-unique record
            if is_read_only(head):
                continue
            kind = "static data member of anonymous-namespace type"
        if innermost in ("block", "block-qualified"):
            continue                    # a local of a mergeable function
        elif innermost == "block-internal":
            if not _HAS_STATIC.search(head):
                continue                # an ordinary local
            if is_read_only(head):
                continue
            kind = "function-local static of an unmergeable function"
        elif innermost != "record":
            if is_read_only(head):
                continue
            borrowed = sorted(t for t in anon_types
                              if re.search(rf"\b{re.escape(t)}\b", head))
            if "anon" in scopes:
                kind = "anonymous-namespace object"
            elif _HAS_STATIC.search(head):
                kind = "file-scope static"
            elif borrowed:
                kind = ("object of anonymous-namespace type "
                        f"`{borrowed[0]}`, so internally linked despite "
                        "`inline`")
            else:
                continue                # external linkage: one per program
        line = code.count("\n", 0, offset + len(stmt) - len(stmt.lstrip())) + 1
        semantic = semantic_code[offset:offset + len(stmt)]
        yield line, kind, normalize_cpp_whitespace_preserve_literals(semantic)


def translation_unit_internal_state(text: str,
                                    named_namespace_provenance: bool = True,
                                    preserve_function_locals: bool = False):
    """Yield mutable storage with exact implementation-file provenance.

    The source layer must not delegate an arbitrary named namespace to the
    artifact layer: the latter intentionally audits only a small owner set, and
    a nested anonymous namespace loses its named parent after demangling. This
    lexical layer therefore reports both named and anonymous namespace objects,
    qualified definitions, and function-local statics. Macro-generated
    VarDecls remain a documented limitation until the gate is backed by
    compile_commands + Clang AST source locations; do not describe this helper
    as an authoritative AST parser.
    """
    spliced = splice_line_continuations(text)
    code = strip_preprocessor(strip_comments_and_strings(spliced))
    semantic_code = strip_preprocessor(strip_comments_preserve_literals(
        spliced))
    if len(semantic_code) != len(code):
        raise ValueError(
            "translation-unit structural/semantic source views misaligned")
    for offset, scopes, stmt in scope_scan(code):
        flat = " ".join(stmt.split())
        candidate = strip_template_heads(flat) if flat else None
        if (not candidate or
                (len(candidate.split()) < 2 and
                 "*" not in candidate and "&" not in candidate) or
                is_non_object_statement(candidate) or
                _TLS_STORAGE.search(candidate)):
            continue              # TLS is governed by exact path+owner rules
        head = object_declarator(candidate)
        if head is None or is_read_only(head):
            continue
        innermost = scope_kind(scopes[-1]) if scopes else "global"
        owner = named_namespace_owner(scopes)
        artifact_owner = (
            not named_namespace_provenance and binary_owner_covers(owner))
        if innermost == "record":
            continue             # declaration; definition is out of record
        elif innermost in ("block", "block-internal", "block-qualified"):
            if not _HAS_STATIC.search(head):
                continue
            if artifact_owner and not preserve_function_locals:
                continue
            kind = "function-local static with source provenance"
        elif "anon" in scopes:
            if artifact_owner:
                continue
            kind = "global anonymous-namespace object"
        elif not scopes:
            if has_qualified_declared_name(head):
                if (not named_namespace_provenance and
                        binary_owner_covers(qualified_declared_owner(head))):
                    continue
                kind = "qualified object definition with source provenance"
            else:
                kind = ("unqualified file-scope static"
                        if _HAS_STATIC.search(head)
                        else "unqualified global object")
        elif innermost == "ns":
            if (not named_namespace_provenance and "anon" not in scopes and
                    binary_owner_covers(owner)):
                continue
            kind = "named-namespace object with source provenance"
        else:
            continue
        line = code.count("\n", 0, offset + len(stmt) - len(stmt.lstrip())) + 1
        semantic = semantic_code[offset:offset + len(stmt)]
        yield line, kind, normalize_cpp_whitespace_preserve_literals(semantic)


_BINARY_BACKSTOP_SOURCE_PREFIXES = (
    "neverc/lib/Plugin/",
    "neverc/lib/DynCode/",
    "neverc/lib/Linker/",
)
_BINARY_BACKSTOP_SOURCE_FILES = {
    "neverc/lib/Foundation/Core/LLVMTimeTraceRootLease.cpp",
}


def source_has_binary_owner_backstop(rel: str) -> bool:
    return (rel in _BINARY_BACKSTOP_SOURCE_FILES or
            any(rel.startswith(prefix)
                for prefix in _BINARY_BACKSTOP_SOURCE_PREFIXES))


def scan_translation_unit_state(report: Report, allowlist: dict,
                                binary_backstop: bool = False,
                                preserve_function_locals: bool = False) -> None:
    """Audit unqualified process storage in configured implementation files."""
    for rel, path in sorted(audited_source_files().items()):
        if path.suffix not in (".c", ".cc", ".cpp"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        named_provenance = not (
            binary_backstop and source_has_binary_owner_backstop(rel))
        for line, kind, stmt in translation_unit_internal_state(
                text, named_namespace_provenance=named_provenance,
                preserve_function_locals=preserve_function_locals):
            if (source_object_allowlisted(rel, stmt + ";", allowlist) or
                    binary_source_object_allowlisted(
                        rel, stmt + ";", allowlist)):
                continue
            report.source_globals.append(
                f"{rel}:{line}: {kind} `{stmt[:80]}`")


def scan_headers(report: Report, allowlist: dict) -> None:
    bases = [ROOT / d for d in HEADER_AUDIT_DIRS]
    report_missing_audit_paths(report, bases, "header")
    for path in sorted(iter_source_files(bases)):
        if path.suffix not in (".h", ".hpp", ".inc"):
            continue
        rel = relative_key(path)
        text = path.read_text(encoding="utf-8", errors="replace")
        for line, kind, stmt in header_internal_state(text):
            # scope_scan deliberately yields the statement without its
            # terminator. Restore it for the declaration-exact allowlist
            # parser; making semicolons globally optional would let a split
            # multi-declarator source line evade the fail-closed check.
            if (allowlisted(rel, stmt + ";", allowlist) or
                    header_constant_allowlisted(rel, stmt + ";", allowlist)):
                continue
            report.headers.append(
                f"{rel}:{line}: {kind} `{stmt[:80]}`")


def validate_allowlist(report: Report, allowlist: dict) -> None:
    """Fail closed when a documented exception is stale or underspecified.

    An allowlist path outside the enumerated dependency closure is a dead rule,
    not evidence. Likewise, every source/TLS exception and complex header
    constant must still resolve to exactly one declaration in its exact file.
    """
    source_files = audited_source_files()
    header_files = {
        relative_key(path): path
        for path in iter_source_files(ROOT / entry
                                      for entry in HEADER_AUDIT_DIRS)
        if path.suffix in (".h", ".hpp", ".inc")
    }

    for entry in allowlist.get("entries", []):
        symbol = entry.get("symbol")
        files = entry.get("files")
        if (not isinstance(symbol, str) or not symbol or
                not isinstance(files, list) or not files or
                not all(isinstance(value, str) and value.strip()
                        for value in files) or
                not all(isinstance(entry.get(field), str) and
                        bool(entry[field].strip())
                        for field in ("declaration", "definition_sha256",
                                      "owner", "lifetime", "justification",
                                      "cleared_by_test"))):
            report.unscannable.append(
                f"invalid TLS allowlist metadata for {symbol!r}")
            continue
        for configured in files:
            rel = configured.replace("\\", "/")
            path = source_files.get(rel)
            if path is None:
                report.unscannable.append(
                    f"TLS allowlist path is outside source audit scope: {rel}")
                continue
            spliced = splice_line_continuations(path.read_text(
                encoding="utf-8", errors="replace"))
            code = strip_comments_and_strings(spliced)
            semantic_code = strip_comments_preserve_literals(spliced)
            exact = sum(
                allowlisted(rel, snippet, {"entries": [entry]})
                for _, _, snippet in iter_tls_declarations(
                    code, semantic_code))
            if exact != 1:
                report.unscannable.append(
                    f"TLS allowlist declaration {rel}::{symbol} matched "
                    f"{exact} times (expected 1)")

    for entry in allowlist.get("header_constants", []):
        rel_value = entry.get("file")
        symbol = entry.get("symbol")
        if (not isinstance(rel_value, str) or not rel_value.strip() or
                not isinstance(symbol, str) or not symbol.strip() or
                not all(isinstance(entry.get(field), str) and
                        bool(entry[field].strip())
                        for field in ("declaration", "definition_sha256",
                                      "owner",
                                      "justification"))):
            report.unscannable.append(
                f"invalid header-constant metadata for {symbol!r}")
            continue
        rel = rel_value.replace("\\", "/")
        path = header_files.get(rel)
        if path is None:
            report.unscannable.append(
                f"header-constant path is outside header audit scope: {rel}")
            continue
        exact = sum(
            header_constant_allowlisted(rel, stmt + ";",
                                        {"header_constants": [entry]})
            for _, _, stmt in header_internal_state(
                path.read_text(encoding="utf-8", errors="replace")))
        if exact != 1:
            report.unscannable.append(
                f"header-constant declaration {rel}::{symbol} matched "
                f"{exact} times (expected 1)")

    for entry in allowlist.get("source_objects", []):
        rel_value = entry.get("file")
        symbol = entry.get("symbol")
        if (not isinstance(rel_value, str) or not rel_value.strip() or
                not isinstance(symbol, str) or not symbol.strip() or
                not all(isinstance(entry.get(field), str) and
                        bool(entry[field].strip())
                        for field in ("declaration", "definition_sha256",
                                      "owner", "kind", "justification"))):
            report.unscannable.append(
                f"invalid source-object metadata for {symbol!r}")
            continue
        rel = rel_value.replace("\\", "/")
        path = source_files.get(rel)
        if path is None or path.suffix not in (".c", ".cc", ".cpp"):
            report.unscannable.append(
                f"source-object path is outside implementation audit scope: "
                f"{rel}")
            continue
        exact = sum(
            source_object_allowlisted(rel, stmt + ";",
                                      {"source_objects": [entry]})
            for _, _, stmt in translation_unit_internal_state(
                path.read_text(encoding="utf-8", errors="replace")))
        if exact != 1:
            report.unscannable.append(
                f"source-object declaration {rel}::{symbol} matched "
                f"{exact} times (expected 1)")

    binary_markers: set[str] = set()
    for entry in allowlist.get("binary_symbols", []):
        minimum = entry.get("min_count")
        maximum = entry.get("max_count")
        artifact_optional = entry.get("artifact_optional", False)
        expected_minimum = 0 if artifact_optional is True else 1
        symbol = entry.get("symbol")
        if (not isinstance(symbol, str) or not symbol.strip() or
                not isinstance(artifact_optional, bool) or
                (artifact_optional and "()::" not in symbol) or
                not isinstance(minimum, int) or isinstance(minimum, bool) or
                minimum != expected_minimum or
                not isinstance(maximum, int) or isinstance(maximum, bool) or
                maximum != 1 or
                not all(isinstance(entry.get(field), str) and
                        bool(entry[field].strip())
                        for field in ("file", "source_symbol", "declaration",
                                      "definition_sha256", "owner", "kind",
                                      "justification"))):
            report.unscannable.append(
                "invalid binary-symbol multiplicity/source metadata for "
                f"{symbol!r}; max_count must equal 1 and min_count must "
                "equal 1 unless a function-local symbol is explicitly "
                "artifact_optional")
            continue
        if symbol in binary_markers:
            report.unscannable.append(
                f"duplicate binary-symbol allowlist marker {symbol!r}")
            continue
        binary_markers.add(symbol)
        rel = entry["file"].replace("\\", "/")
        path = source_files.get(rel)
        if path is None or path.suffix not in (".c", ".cc", ".cpp"):
            report.unscannable.append(
                "binary-symbol source path is outside implementation audit "
                f"scope: {rel}")
            continue
        exact = sum(
            binary_source_object_allowlisted(
                rel, stmt + ";", {"binary_symbols": [entry]})
            for _, _, stmt in translation_unit_internal_state(
                path.read_text(encoding="utf-8", errors="replace")))
        if exact != 1:
            report.unscannable.append(
                f"binary-symbol source declaration "
                f"{rel}::{entry['source_symbol']} matched {exact} times "
                "(expected 1)")


# Demangled-symbol namespaces whose writable data must be audited.  The final
# linked compiler is the authoritative shipped artifact: scanning it (rather than
# loose object files) automatically excludes stale objects left behind by removed
# sources, unit-test object files and fuzz-only support, which are never linked
# into the shipped `neverc`.
#
# This includes the ``linker::`` namespace: linker state participates in the
# same in-process concurrent invocation boundary as plugin and dyncode state.
#
# Known limit: a global anonymous namespace demangles to a bare ``(anonymous
# namespace)::X``. Blanket-matching that spelling would mix audited NeverC
# objects with the many LLVM/Clang objects linked into the same compiler, so
# the binary layer cannot assign it reliable source provenance. The lexical
# implementation scan above covers ordinary declarations by exact path; macro-
# generated VarDecls require the planned Clang-AST-backed provenance layer.
_BINARY_SCOPES = (
    "neverc::plugin",
    "neverc::dyncode",
    "neverc::time_trace_detail",
    "linker",
)
# Some symbols reach us still mangled: nm's demangler gives up on Mach-O
# decorations such as the ``$tlv$init`` suffix and returns the raw Itanium
# spelling.  Matching those too keeps a symbol from slipping through merely
# because it could not be demangled.
_BINARY_SCOPE_MANGLED = re.compile(
    r"^_?_ZZ?N[rVKRO]*(?:"
    r"6neverc(?:6plugin|7dyncode|17time_trace_detail)|6linker)")
_BINARY_AUDIT_EXTERNAL_SENTINEL = (
    "neverc::time_trace_detail::LLVMTimeTraceRootGeneration")
_BINARY_AUDIT_LOCAL_SENTINEL = (
    "neverc::plugin::(anonymous namespace)::ExclusiveWaitEpoch")
# Canonical demangler spellings for compiler-emitted writable data that is not
# plugin state. These must stay anchored: substring matching would hide user
# globals such as ``VTableCache`` and ``typeinfoRegistry``.
_BINARY_NOISE_PREFIXES = (
    "guard variable for ",
    "typeinfo for ",
    "typeinfo name for ",
    "vtable for ",
    "construction vtable for ",
    "vtt for ",
)
_BINARY_NOISE_MANGLED = re.compile(r"^_?_Z(?:TV|TI|TS|GV)")

# Names the linked compiler can have.  Windows builds produce `neverc.exe`, and
# looking only for the extensionless spelling used to make the scan report a
# missing binary there -- indistinguishable from a mistyped --build-dir.
_COMPILER_NAMES = ("neverc", "neverc.exe")

# On ELF, ``.data.rel.ro`` is a SHF_WRITE section and therefore reported as `d`,
# even though the dynamic loader maps it read-only once relocations are applied
# (RELRO).  Every constant table holding a function pointer lands there, so
# classifying by letter alone would flag all of them.  Nothing dynamically
# initialized can live in ``.data.rel.ro`` -- the initializer would run after
# the segment is already read-only -- so such objects go to ``.data``/``.bss``
# and are still caught below.
_WRITABLE_CLASSES = frozenset("dDbBgGVvuSsCcWw")
_ELF_READ_ONLY_SECTION_PREFIXES = (
    ".rodata", ".data.rel.ro", ".text", ".init", ".fini", ".eh_frame",
    ".gcc_except_table", ".note", ".debug",
)
_ELF_WRITABLE_SECTION_PREFIXES = (
    ".data", ".bss", ".sdata", ".sbss", ".got", ".dynamic",
)
_ELF_UNDEFINED_SECTIONS = ("*UND*", "UND", "UNDEF")
_ELF_COMMON_SECTIONS = ("*COM*", "COMMON", ".common")
_READ_ONLY_MACHO_SEGMENTS = {
    "__TEXT", "__TEXT_EXEC", "__DATA_CONST", "__AUTH_CONST", "__LINKEDIT",
    "__LLVM", "__DWARF", "__PAGEZERO",
}

_ELF_SECTION_ROW = re.compile(
    r"^\s*\[\s*\d+\]\s+(?P<name>\S+)\s+\S+\s+"
    r"(?P<address>[0-9A-Fa-f]+)\s+\S+\s+"
    r"(?P<size>[0-9A-Fa-f]+)\s+\S+\s+(?P<flags>\S*)",
    re.MULTILINE,
)
_ELF_RELRO_ROW = re.compile(
    r"^\s*GNU_RELRO\s+\S+\s+"
    r"(?P<address>0x[0-9A-Fa-f]+)\s+\S+\s+\S+\s+"
    r"(?P<size>0x[0-9A-Fa-f]+)\s+",
    re.MULTILINE,
)


def parse_elf_section_protections(section_output: str,
                                  program_output: str) -> dict[str, bool]:
    """Return section -> runtime-read-only from readelf's numeric facts."""
    relro_ranges = []
    for match in _ELF_RELRO_ROW.finditer(program_output):
        start = int(match.group("address"), 16)
        size = int(match.group("size"), 16)
        if size:
            relro_ranges.append((start, start + size))

    protections: dict[str, bool] = {}
    for match in _ELF_SECTION_ROW.finditer(section_output):
        name = match.group("name")
        address = int(match.group("address"), 16)
        size = int(match.group("size"), 16)
        flags = match.group("flags")
        in_relro = bool(size) and any(
            start <= address and address + size <= end
            for start, end in relro_ranges)
        protections[name] = "W" not in flags or in_relro
    return protections


def parse_elf_tls_sections(section_output: str) -> set[str]:
    """Return sections whose ELF header has the real SHF_TLS flag."""
    return {
        match.group("name")
        for match in _ELF_SECTION_ROW.finditer(section_output)
        if "T" in match.group("flags")
    }


def parse_macho_segment_protections(load_commands: str) -> dict[str, bool]:
    """Return segment -> runtime-read-only from Mach-O load commands.

    ``SG_READ_ONLY`` (0x10) is dyld's post-fixup protection contract; otherwise
    the actual initial VM write bit is authoritative. Segment names alone are
    intentionally ignored.
    """
    protections: dict[str, bool] = {}
    blocks = re.split(r"^\s*cmd\s+LC_SEGMENT(?:_64)?\s*$",
                      load_commands, flags=re.MULTILINE)
    for block in blocks[1:]:
        segment = re.search(r"^\s*segname\s+(\S+)\s*$", block,
                            re.MULTILINE)
        initprot = re.search(r"^\s*initprot\s+(0x[0-9A-Fa-f]+)\s*$",
                             block, re.MULTILINE)
        flags = re.search(r"^\s*flags\s+(0x[0-9A-Fa-f]+)\s*$", block,
                          re.MULTILINE)
        if segment is None or initprot is None or flags is None:
            continue
        protection_bits = int(initprot.group(1), 16)
        segment_flags = int(flags.group(1), 16)
        protections[segment.group(1)] = (
            not (protection_bits & 0x2) or bool(segment_flags & 0x10))
    return protections


def parse_macho_tls_sections(load_commands: str) -> set[str]:
    """Return Mach-O sections with an S_THREAD_LOCAL_* section type."""
    tls_types = frozenset(range(0x11, 0x16))
    result: set[str] = set()
    for section in re.split(r"^\s*Section\s*$", load_commands,
                            flags=re.MULTILINE)[1:]:
        name = re.search(r"^\s*sectname\s+(\S+)\s*$", section,
                         re.MULTILINE)
        segment = re.search(r"^\s*segname\s+(\S+)\s*$", section,
                            re.MULTILINE)
        flags = re.search(r"^\s*flags\s+(0x[0-9A-Fa-f]+)\b", section,
                          re.MULTILINE)
        if name is None or segment is None or flags is None:
            continue
        if int(flags.group(1), 16) & 0xff in tls_types:
            result.add(f"{segment.group(1)},{name.group(1)}")
    return result


def artifact_section_protections(binary: pathlib.Path,
                                 image_format: str) -> dict[str, bool] | None:
    """Read real runtime protections, or return None for fail-closed audit."""
    if image_format == "macho-thin":
        otool = ("/usr/bin/otool" if pathlib.Path("/usr/bin/otool").exists()
                 else shutil.which("otool"))
        if otool is None:
            return None
        try:
            result = subprocess.run([otool, "-l", str(binary)],
                                    capture_output=True, text=True,
                                    check=False)
        except OSError:
            return None
        if result.returncode != 0 or not result.stdout:
            return None
        parsed = parse_macho_segment_protections(result.stdout)
        return parsed or None
    if image_format == "elf":
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if readelf is None:
            return None
        try:
            sections = subprocess.run(
                [readelf, "-W", "-S", str(binary)], capture_output=True,
                text=True, check=False)
            programs = subprocess.run(
                [readelf, "-W", "-l", str(binary)], capture_output=True,
                text=True, check=False)
        except OSError:
            return None
        if (sections.returncode != 0 or programs.returncode != 0 or
                not sections.stdout):
            return None
        parsed = parse_elf_section_protections(sections.stdout,
                                               programs.stdout)
        return parsed or None
    return None


def artifact_tls_sections(binary: pathlib.Path,
                          image_format: str) -> set[str] | None:
    """Read TLS section *flags*, never names; None means no proof available."""
    if image_format == "macho-thin":
        otool = ("/usr/bin/otool" if pathlib.Path("/usr/bin/otool").exists()
                 else shutil.which("otool"))
        if otool is None:
            return None
        try:
            result = subprocess.run([otool, "-l", str(binary)],
                                    capture_output=True, text=True,
                                    check=False)
        except OSError:
            return None
        if result.returncode != 0 or not result.stdout:
            return None
        return parse_macho_tls_sections(result.stdout)
    if image_format == "elf":
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if readelf is None:
            return None
        try:
            result = subprocess.run(
                [readelf, "-W", "-S", str(binary)], capture_output=True,
                text=True, check=False)
        except OSError:
            return None
        if result.returncode != 0 or not result.stdout:
            return None
        return parse_elf_tls_sections(result.stdout)
    return None

# Thread-local storage, which this layer skips (see the module docstring).  ELF
# keeps a TLS object in ``.tbss``/``.tdata``; Mach-O splits it into a
# ``__thread_vars`` descriptor plus a ``$tlv$init`` storage symbol whose suffix
# also defeats the demangler.  Both spellings are recognised so the exclusion
# does not depend on which nm produced the listing.
_TLS_ELF_PREFIXES = (".tbss", ".tdata")
_TLS_MACHO_SECTION_NAMES = {
    "__thread_vars", "__thread_bss", "__thread_data", "__thread_init",
}
_TLS_MACHO_MARKER = "$tlv$"


def symbol_is_thread_local(name: str, section: str) -> bool:
    """Report whether artifact syntax marks *name* as a TLS candidate.

    This is deliberately not permission to skip the symbol.  The binary audit
    skips a candidate only when its exact source identifier is present in the
    validated TLS manifest; otherwise a writable object can evade the gate by
    choosing a TLS-looking section or ``$tlv$`` substring.
    """
    if _TLS_MACHO_MARKER in name:
        return True
    if "," in section:
        _, section_name = section.split(",", 1)
        if section_name in _TLS_MACHO_SECTION_NAMES:
            return True
    return any(
        section == prefix or section.startswith(prefix + ".")
        for prefix in _TLS_ELF_PREFIXES
    )


def allowlisted_binary_tls_symbol(name: str, allowlist: dict) -> bool:
    """Bind an nm TLS spelling to one exact source-manifest identifier."""
    for entry in allowlist.get("entries", []):
        symbol = entry.get("symbol")
        if not isinstance(symbol, str) or not symbol:
            continue
        if name == symbol or name.endswith("::" + symbol):
            return True
        # Mach-O nm may leave the Itanium name mangled when it appends the
        # non-Itanium ``$tlv$init`` suffix. Match the final length-prefixed
        # identifier only, never a substring in an ordinary user symbol.
        marker = rf"{len(symbol)}{re.escape(symbol)}E(?:\$tlv\$init)?$"
        if re.search(marker, name):
            return True
    return False


def symbol_in_audited_scope(name: str) -> bool:
    """Report whether *name* lives in an audited namespace, demangled or not."""
    owner = name
    low = owner.lower()
    for prefix in _BINARY_NOISE_PREFIXES:
        if low.startswith(prefix):
            owner = owner[len(prefix):].lstrip()
            break
    if any(owner == scope or owner.startswith(scope + "::")
           for scope in _BINARY_SCOPES):
        return True
    return _BINARY_SCOPE_MANGLED.match(name) is not None


def symbol_is_compiler_noise(name: str) -> bool:
    """Report whether *name* is compiler-emitted data rather than plugin state."""
    low = name.lower()
    if low.startswith(_BINARY_NOISE_PREFIXES):
        return True
    return _BINARY_NOISE_MANGLED.match(name) is not None


def symbol_is_writable(sym_class: str, section: str,
                       runtime_read_only: bool | None = None) -> bool:
    """Decide whether a symbol lives in storage that stays writable at runtime.

    ELF uses the SysV section/class. Mach-O must arrive through Darwin format:
    S/s only means "not a canonical text/data/bss class" and is not a
    writability verdict. Known read-only segments are rejected; every other
    Mach segment is conservatively writable.
    """
    if section in _ELF_UNDEFINED_SECTIONS:
        return False
    if runtime_read_only is True:
        return False
    if runtime_read_only is False:
        return True
    if section in _ELF_COMMON_SECTIONS:
        return True
    # A writable/weak-object nm class is stronger evidence than a conventional
    # section name. Runtime flags above may override it; names may not.
    if sym_class in _WRITABLE_CLASSES:
        return True
    if "," in section:
        return True
    if any(section == prefix or section.startswith(prefix + ".")
           for prefix in _ELF_READ_ONLY_SECTION_PREFIXES):
        return False
    if any(section == prefix or section.startswith(prefix + ".")
           for prefix in _ELF_WRITABLE_SECTION_PREFIXES):
        return True
    # With no recognised section, accept only nm classes that positively prove
    # the symbol has no writable object storage. Everything else is unknown and
    # must fail closed rather than silently turning an unfamiliar tool dialect
    # into a clean audit.
    return sym_class not in frozenset("aAiInNpPrRtT-")


class NmReadStatus(Enum):
    OK = "ok"
    UNAVAILABLE = "unavailable"
    ERROR = "error"


@dataclass(frozen=True)
class NmReadResult:
    status: NmReadStatus
    symbols: tuple[tuple[str, str, str], ...] = ()
    diagnostics: tuple[str, ...] = ()


_NM_CLASS = re.compile(r"^[A-Za-z?\-]$")
_NM_ADDRESS = re.compile(r"^(?:[0-9A-Fa-f]+|-+)$")
_NM_SYSV_TYPE = re.compile(r"^[A-Za-z0-9_?@.+\-]+$")
_NM_SYSV_LINE = re.compile(r"^[0-9]+(?::[0-9]+)?$")
_NM_SYSV_SEPARATOR = re.compile(r"^[|+\-\s]+$")
_NM_DARWIN_STATUS = (
    r"weak external automatically hidden|"
    r"non-external \(was a private external\)|"
    r"weak private external|private external|weak external|"
    r"non-external|external"
)
_NM_DARWIN_FLAGS = (
    r"(?:\s+\[(?:no dead strip|symbol resolver|alt entry|cold func|Thumb)\])*"
)
_NM_DARWIN_ROW = re.compile(
    rf"^(?P<address>[0-9A-Fa-f]{{8}}|[0-9A-Fa-f]{{16}})\s+"
    rf"\((?P<location>[^()]*)\)\s+"
    rf"(?:(?:referenced dynamically|\[referenced dynamically\])\s+)?"
    rf"(?P<status>{_NM_DARWIN_STATUS})"
    rf"{_NM_DARWIN_FLAGS}\s+(?P<name>.+)$")
_NM_DARWIN_SECTION = re.compile(
    r"(?P<segment>[^,()\s?]+),(?P<section>[^,()\s?]+)")

_MACHO_THIN_MAGICS = {
    bytes.fromhex(value) for value in (
        "feedface", "cefaedfe", "feedfacf", "cffaedfe")
}
_MACHO_FAT_MAGICS = {
    bytes.fromhex(value) for value in (
        "cafebabe", "bebafeca", "cafebabf", "bfbafeca")
}


def _parse_nm_sysv(output: str):
    """Parse a complete SysV listing, returning ``None`` if any row is alien."""
    rows: list[tuple[str, str, str]] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith("Symbols from ") and line.endswith(":"):
            continue
        if line.split() == [
                "Name", "Value", "Class", "Type", "Size", "Line",
                "Section"]:
            continue
        if _NM_SYSV_SEPARATOR.fullmatch(line):
            continue
        fields = [field.strip() for field in line.rsplit("|", 6)]
        if len(fields) != 7:
            return None
        if (fields[0] == "Name" and fields[1] == "Value" and
                fields[2] == "Class" and fields[6] == "Section"):
            continue
        if not fields[0] or not _NM_CLASS.fullmatch(fields[2]):
            return None
        if fields[1] and not _NM_ADDRESS.fullmatch(fields[1]):
            return None
        if fields[3] and not _NM_SYSV_TYPE.fullmatch(fields[3]):
            return None
        if fields[4] and not _NM_ADDRESS.fullmatch(fields[4]):
            return None
        if fields[5] and not _NM_SYSV_LINE.fullmatch(fields[5]):
            return None
        rows.append((fields[0], fields[2], fields[6]))
    return tuple(rows)


def _parse_nm_bsd(output: str):
    """Parse a complete BSD/default listing without accepting partial rows."""
    rows: list[tuple[str, str, str]] = []
    for raw_line in output.splitlines():
        parts = raw_line.split()
        if not parts:
            continue
        section = ""
        if len(parts) >= 3 and _NM_ADDRESS.fullmatch(parts[0]):
            sym_class = parts[1]
            name = " ".join(parts[2:])
        elif len(parts) >= 2 and parts[0] in ("U", "V", "v", "W", "w"):
            sym_class = parts[0]
            name = " ".join(parts[1:])
            section = "*UND*"
        else:
            return None
        if not _NM_CLASS.fullmatch(sym_class) or not name:
            return None
        rows.append((name, sym_class, section))
    return tuple(rows)


def _parse_nm_darwin(output: str):
    """Parse a complete thin Mach-O Darwin listing.

    ``-U`` removes undefined symbols. Absolute and indirect symbols carry no
    writable storage and are recognised then skipped; common symbols are
    conservatively mapped to ``__DATA,__common``. Any other non-empty line is
    alien and rejects the entire listing rather than returning partial proof.
    """
    rows: list[tuple[str, str, str]] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        match = _NM_DARWIN_ROW.fullmatch(line)
        if not match:
            return None
        location = match.group("location")
        section_match = _NM_DARWIN_SECTION.fullmatch(location)
        if section_match:
            section = (f"{section_match.group('segment')},"
                       f"{section_match.group('section')}")
        elif location in ("absolute", "indirect"):
            continue
        elif location == "common":
            section = "__DATA,__common"
        else:
            return None
        status = match.group("status")
        sym_class = "s" if status.startswith("non-external") else "S"
        rows.append((match.group("name"), sym_class, section))
    return tuple(rows)


def _demangle_macho_symbols(symbols):
    """Canonicalise Darwin's platform-prefixed Itanium names in one batch."""
    indexes = [
        index for index, (name, _, _) in enumerate(symbols)
        if re.match(r"^__?_Z", name)
    ]
    if not indexes:
        return tuple(symbols), ()

    demangler = None
    for candidate in ("llvm-cxxfilt", "c++filt"):
        demangler = shutil.which(candidate)
        if demangler:
            break
    if demangler is None:
        return None, ("llvm-cxxfilt/c++filt unavailable for Mach-O names",)

    names = [symbols[index][0] for index in indexes]
    try:
        process = subprocess.run(
            [demangler, "--strip-underscore"],
            input="\n".join(names) + "\n",
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        return None, (f"unable to execute {demangler}: {exc}",)
    if process.returncode != 0:
        return None, (_nm_failure(demangler, "demangle", process),)
    if _nm_has_error_diagnostic(process.stderr):
        return None, (
            f"{demangler} emitted an error diagnostic despite exit 0",)
    demangled = process.stdout.splitlines()
    if len(demangled) != len(indexes) or any(not name for name in demangled):
        return None, (
            f"{demangler} returned {len(demangled)} names for "
            f"{len(indexes)} Mach-O symbols",)

    result = list(symbols)
    for index, name in zip(indexes, demangled):
        _, sym_class, section = result[index]
        result[index] = (name, sym_class, section)
    return tuple(result), ()


def _nm_failure(nm: str, mode: str, process) -> str:
    detail = (process.stderr or process.stdout).strip().splitlines()
    suffix = f": {detail[0][:160]}" if detail else ""
    return f"{nm} {mode} exited {process.returncode}{suffix}"


def _nm_has_error_diagnostic(stderr: str) -> bool:
    """Recognise tool errors without rejecting harmless platform warnings."""
    return any(
        re.search(r"(?:^|:\s*)error(?:\[[^]]+\])?:", line,
                  flags=re.IGNORECASE)
        for line in stderr.splitlines()
    )


def _nm_macho_symbols(nm: str, binary: pathlib.Path) -> NmReadResult:
    """Read a complete thin Mach-O nlist with segment/section provenance."""
    diagnostics: list[str] = []
    modes = (
        ("Darwin", [nm, "-U", "--no-demangle", "--no-dyldinfo",
                    "--format=darwin", str(binary)]),
        ("classic Darwin", [nm, "-U", "-m", str(binary)]),
    )
    for mode, args in modes:
        try:
            process = subprocess.run(
                args,
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as exc:
            diagnostics.append(f"unable to execute {nm} in {mode}: {exc}")
            break
        if process.returncode != 0:
            diagnostics.append(_nm_failure(nm, mode, process))
            continue
        if _nm_has_error_diagnostic(process.stderr):
            diagnostics.append(
                f"{nm} {mode} emitted an error diagnostic despite exit 0")
            continue
        rows = _parse_nm_darwin(process.stdout)
        if rows is None:
            diagnostics.append(f"{nm} {mode} output was malformed")
            continue
        demangled, demangle_diagnostics = _demangle_macho_symbols(rows)
        if demangled is None:
            diagnostics.extend(demangle_diagnostics)
            continue
        return NmReadResult(NmReadStatus.OK, demangled)
    return NmReadResult(NmReadStatus.ERROR,
                        diagnostics=tuple(diagnostics))


def _nm_symbols(nm: str, binary: pathlib.Path,
                mach_o: bool = False) -> NmReadResult:
    """Read one complete symbol table with one installed nm implementation.

    Mach-O requires Darwin segment/section output. ELF prefers SysV because it
    carries sections, then falls back to BSD for the same tool. Every mode
    succeeds only when the process returns zero and every non-empty row is
    recognised.
    """
    if mach_o:
        return _nm_macho_symbols(nm, binary)

    diagnostics: list[str] = []
    try:
        sysv = subprocess.run(
            [nm, "-C", "--format=sysv", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        return NmReadResult(
            NmReadStatus.ERROR,
            diagnostics=(f"unable to execute {nm}: {exc}",))
    if sysv.returncode == 0 and not _nm_has_error_diagnostic(sysv.stderr):
        rows = _parse_nm_sysv(sysv.stdout)
        if rows is not None:
            return NmReadResult(NmReadStatus.OK, rows)
        diagnostics.append(f"{nm} SysV output was malformed")
    elif sysv.returncode == 0:
        diagnostics.append(
            f"{nm} SysV emitted an error diagnostic despite exit 0")
    else:
        diagnostics.append(_nm_failure(nm, "SysV", sysv))

    try:
        bsd = subprocess.run(
            [nm, "-C", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        diagnostics.append(f"unable to execute {nm} in BSD mode: {exc}")
        return NmReadResult(NmReadStatus.ERROR,
                            diagnostics=tuple(diagnostics))
    if bsd.returncode != 0:
        diagnostics.append(_nm_failure(nm, "BSD", bsd))
        return NmReadResult(NmReadStatus.ERROR,
                            diagnostics=tuple(diagnostics))
    if _nm_has_error_diagnostic(bsd.stderr):
        diagnostics.append(
            f"{nm} BSD emitted an error diagnostic despite exit 0")
        return NmReadResult(NmReadStatus.ERROR,
                            diagnostics=tuple(diagnostics))
    rows = _parse_nm_bsd(bsd.stdout)
    if rows is None:
        diagnostics.append(f"{nm} BSD output was malformed")
        return NmReadResult(NmReadStatus.ERROR,
                            diagnostics=tuple(diagnostics))
    return NmReadResult(NmReadStatus.OK, rows)


def _runtime_read_only(section: str,
                       protections: dict[str, bool] | None) -> bool | None:
    if protections is None:
        return None
    if section in protections:
        return protections[section]
    if "," in section:
        segment, _ = section.split(",", 1)
        return protections.get(segment)
    return None


def audit_symbols(symbols, allowlist: dict,
                  section_protections: dict[str, bool] | None = None,
                  verified_tls_sections: set[str] | None = None
                  ) -> list[str]:
    """Return findings for the writable, in-scope symbols of an nm listing.

    Kept separate from :func:`scan_binary` so the classification can be checked
    against synthetic ELF and Mach-O listings -- the two disagreed once, and a
    test only catches that if it can supply both without a linked compiler.
    """
    findings: list[str] = []
    seen: set[str] = set()
    allowed_counts: dict[str, int] = {}
    for name, sym_class, section in symbols:
        if (verified_tls_sections is not None and
                section in verified_tls_sections and
                allowlisted_binary_tls_symbol(name, allowlist)):
            continue
        if not symbol_is_writable(
                sym_class, section,
                runtime_read_only=_runtime_read_only(
                    section, section_protections)):
            continue
        if not symbol_in_audited_scope(name):
            continue
        if symbol_is_compiler_noise(name):
            continue
        entry = binary_allowlist_entry(name, allowlist)
        if entry is not None:
            marker = entry.get("symbol", name)
            count = allowed_counts.get(marker, 0) + 1
            allowed_counts[marker] = count
            maximum = entry.get("max_count", 1)
            valid_maximum = (isinstance(maximum, int) and
                             not isinstance(maximum, bool) and maximum > 0)
            if valid_maximum and count <= maximum:
                continue
            if count == (maximum + 1 if valid_maximum else 1):
                findings.append(
                    f"neverc: allowlisted writable symbol `{marker}` appears "
                    f"{count} times (max {maximum!r})")
            continue
        if name in seen:
            continue
        seen.add(name)
        where = section or sym_class
        findings.append(f"neverc: writable symbol `{name}` ({where})")
    for entry in allowlist.get("binary_symbols", []):
        if "min_count" not in entry:
            continue
        marker = entry.get("symbol")
        minimum = entry.get("min_count")
        if (not isinstance(marker, str) or
                not isinstance(minimum, int) or isinstance(minimum, bool) or
                minimum < 0):
            continue
        count = allowed_counts.get(marker, 0)
        if count < minimum:
            findings.append(
                f"neverc: allowlisted writable symbol `{marker}` appears "
                f"{count} times (min {minimum})")
    return findings


def locate_compiler(build_dir: pathlib.Path) -> pathlib.Path | None:
    """Return the linked compiler under *build_dir*, whatever the host names it."""
    for name in _COMPILER_NAMES:
        candidate = build_dir / "bin" / name
        if candidate.exists():
            return candidate
    return None


def binary_image_format(binary: pathlib.Path) -> tuple[str | None, str | None]:
    """Return the image family determined from its first four bytes."""
    try:
        with binary.open("rb") as stream:
            magic = stream.read(4)
    except OSError as exc:
        return None, f"unable to read binary magic from {binary}: {exc}"
    if magic.startswith(b"MZ"):
        return "pe", None
    if magic == b"\x7fELF":
        return "elf", None
    if magic in _MACHO_THIN_MAGICS:
        return "macho-thin", None
    if magic in _MACHO_FAT_MAGICS:
        return "macho-fat", None
    return "unknown", f"unrecognised binary magic {magic.hex() or '<empty>'}"


_COFF_IMAGE_SCN_MEM_WRITE = 0x80000000
_COFF_IMAGE_FILE_EXECUTABLE_IMAGE = 0x2
_COFF_IMAGE_SYM_CLASS_EXTERNAL = 0x2
_COFF_IMAGE_SYM_CLASS_STATIC = 0x3
_COFF_FORMATS = frozenset(("COFF-x86-64", "COFF-ARM64"))


@dataclass(frozen=True)
class CoffSection:
    number: int
    name: str
    virtual_address: int
    characteristics: int


@dataclass(frozen=True)
class CoffSymbol:
    raw_name: str
    name: str
    value: int
    section_number: int
    section_name: str
    section_characteristics: int
    storage_class: int
    aux_symbol_count: int
    thread_local: bool


@dataclass(frozen=True)
class CoffReadResult:
    status: NmReadStatus
    symbols: tuple[CoffSymbol, ...] = ()
    diagnostics: tuple[str, ...] = ()


def _coff_number(text: str) -> int:
    if re.fullmatch(r"0[xX][0-9A-Fa-f]+", text):
        return int(text, 16)
    if re.fullmatch(r"-?[0-9]+", text):
        return int(text, 10)
    raise ValueError(f"invalid integer {text!r}")


def _coff_one_field(block: list[str], key: str) -> str:
    prefix = f"{key}: "
    values = [line.strip()[len(prefix):]
              for line in block if line.strip().startswith(prefix)]
    if len(values) != 1:
        raise ValueError(f"expected one {key} field, found {len(values)}")
    return values[0]


def _coff_outer_block(lines: list[str], opener: str,
                      closer: str) -> list[str]:
    indexes = [index for index, line in enumerate(lines) if line == opener]
    if len(indexes) != 1:
        raise ValueError(f"expected one {opener!r}, found {len(indexes)}")
    start = indexes[0] + 1
    try:
        end = lines.index(closer, start)
    except ValueError as exc:
        raise ValueError(f"unterminated {opener!r}") from exc
    return lines[start:end]


def _coff_item_blocks(container: list[str], item: str) -> list[list[str]]:
    opener = f"  {item} {{"
    blocks: list[list[str]] = []
    index = 0
    while index < len(container):
        if not container[index].strip():
            index += 1
            continue
        if container[index] != opener:
            raise ValueError(
                f"unexpected line in {item} list: {container[index]!r}")
        start = index + 1
        index = start
        while index < len(container) and container[index] != "  }":
            index += 1
        if index == len(container):
            raise ValueError(f"unterminated {item} block")
        blocks.append(container[start:index])
        index += 1
    return blocks


def _coff_characteristics(block: list[str]) -> int:
    matches = []
    for line in block:
        match = re.fullmatch(
            r"\s*Characteristics \[ \(0[xX]([0-9A-Fa-f]+)\)", line)
        if match:
            matches.append(int(match.group(1), 16))
    if len(matches) != 1:
        raise ValueError(
            f"expected one Characteristics field, found {len(matches)}")
    return matches[0]


def _coff_section_name(field: str) -> str:
    match = re.fullmatch(
        r"(?P<name>.*?)(?: \((?:[0-9A-Fa-f]{2})(?: [0-9A-Fa-f]{2})*\))?",
        field)
    if match is None or not match.group("name"):
        raise ValueError(f"invalid COFF section name {field!r}")
    return match.group("name")


def _parse_coff_readobj(output: str) -> tuple[CoffSymbol, ...]:
    """Parse a complete llvm-readobj PE listing or reject it atomically."""
    lines = output.splitlines()
    formats = [line[len("Format: "):] for line in lines
               if line.startswith("Format: ")]
    if len(formats) != 1 or formats[0] not in _COFF_FORMATS:
        raise ValueError(f"unsupported or ambiguous COFF format {formats!r}")
    files = [line[len("File: "):] for line in lines
             if line.startswith("File: ")]
    if len(files) != 1 or not files[0]:
        raise ValueError(f"expected one PE input, found {files!r}")
    address_sizes = [line[len("AddressSize: "):] for line in lines
                     if line.startswith("AddressSize: ")]
    if address_sizes != ["64bit"]:
        raise ValueError(f"unexpected PE address size {address_sizes!r}")
    arches = [line[len("Arch: "):] for line in lines
              if line.startswith("Arch: ")]
    expected_arch = {
        "COFF-x86-64": "x86_64",
        "COFF-ARM64": "aarch64",
    }[formats[0]]
    if arches != [expected_arch]:
        raise ValueError(
            f"COFF format/architecture disagree: {formats[0]} vs {arches!r}")

    header = _coff_outer_block(lines, "ImageFileHeader {", "}")
    machine = _coff_one_field(header, "Machine")
    expected_machine = {
        "COFF-x86-64": "IMAGE_FILE_MACHINE_AMD64 (0x8664)",
        "COFF-ARM64": "IMAGE_FILE_MACHINE_ARM64 (0xAA64)",
    }[formats[0]]
    if machine != expected_machine:
        raise ValueError(
            f"COFF format/machine disagree: {formats[0]} vs {machine}")
    section_count = _coff_number(_coff_one_field(header, "SectionCount"))
    symbol_pointer = _coff_number(
        _coff_one_field(header, "PointerToSymbolTable"))
    symbol_count = _coff_number(_coff_one_field(header, "SymbolCount"))
    optional_size = _coff_number(
        _coff_one_field(header, "OptionalHeaderSize"))
    header_characteristics = _coff_characteristics(header)
    if (section_count <= 0 or optional_size <= 0 or
            not (header_characteristics &
                 _COFF_IMAGE_FILE_EXECUTABLE_IMAGE)):
        raise ValueError("COFF input is not an executable PE image")
    if lines.count("ImageOptionalHeader {") != 1:
        raise ValueError("PE image has no unique optional header")
    optional_header = _coff_outer_block(
        lines, "ImageOptionalHeader {", "}")
    image_base = _coff_number(
        _coff_one_field(optional_header, "ImageBase"))
    if symbol_pointer <= 0 or symbol_count <= 0:
        raise ValueError(
            "PE image has no embedded COFF static symbol table")

    section_container = _coff_outer_block(lines, "Sections [", "]")
    sections: dict[int, CoffSection] = {}
    for block in _coff_item_blocks(section_container, "Section"):
        number = _coff_number(_coff_one_field(block, "Number"))
        if number <= 0 or number in sections:
            raise ValueError(f"invalid or duplicate section number {number}")
        section = CoffSection(
            number=number,
            name=_coff_section_name(_coff_one_field(block, "Name")),
            virtual_address=_coff_number(
                _coff_one_field(block, "VirtualAddress")),
            characteristics=_coff_characteristics(block),
        )
        sections[number] = section
    if len(sections) != section_count or set(sections) != set(
            range(1, section_count + 1)):
        raise ValueError(
            "section listing does not match ImageFileHeader SectionCount")

    tls_block = _coff_outer_block(lines, "TLSDirectory {", "}")
    tls_fields = [line for line in tls_block if line.strip()]
    tls_start = 0
    tls_limit = 0
    if tls_fields:
        tls_start = _coff_number(
            _coff_one_field(tls_block, "StartAddressOfRawData"))
        tls_end = _coff_number(
            _coff_one_field(tls_block, "EndAddressOfRawData"))
        tls_zero_fill = _coff_number(
            _coff_one_field(tls_block, "SizeOfZeroFill"))
        if (tls_start < image_base or tls_end < tls_start or
                tls_zero_fill < 0):
            raise ValueError("invalid PE TLS address range")
        tls_limit = tls_end + tls_zero_fill

    symbol_container = _coff_outer_block(lines, "Symbols [", "]")
    symbols: list[CoffSymbol] = []
    table_entries = 0
    for block in _coff_item_blocks(symbol_container, "Symbol"):
        raw_name = _coff_one_field(block, "Name")
        if not raw_name:
            raise ValueError("COFF symbol has an empty name")
        value = _coff_number(_coff_one_field(block, "Value"))
        section_field = _coff_one_field(block, "Section")
        section_match = re.fullmatch(r"(?P<name>.*) \((?P<number>-?[0-9]+)\)",
                                     section_field)
        if section_match is None:
            raise ValueError(f"invalid symbol section {section_field!r}")
        section_number = int(section_match.group("number"), 10)
        storage_field = _coff_one_field(block, "StorageClass")
        storage_match = re.fullmatch(r".* \(0[xX]([0-9A-Fa-f]+)\)",
                                     storage_field)
        if storage_match is None:
            raise ValueError(f"invalid storage class {storage_field!r}")
        storage_class = int(storage_match.group(1), 16)
        aux_count = _coff_number(
            _coff_one_field(block, "AuxSymbolCount"))
        if aux_count < 0:
            raise ValueError("negative COFF auxiliary-symbol count")
        table_entries += 1 + aux_count

        if section_number > 0:
            section = sections.get(section_number)
            if section is None:
                raise ValueError(
                    f"symbol refers to absent section {section_number}")
            if section_match.group("name") != section.name:
                raise ValueError(
                    f"symbol section name/number disagree for {raw_name!r}")
            section_name = section.name
            section_characteristics = section.characteristics
            symbol_va = image_base + section.virtual_address + value
            thread_local = tls_start <= symbol_va < tls_limit
        elif section_number in (-2, -1, 0):
            section_name = section_match.group("name")
            section_characteristics = 0
            thread_local = False
        else:
            raise ValueError(
                f"symbol has invalid section number {section_number}")

        symbols.append(CoffSymbol(
            raw_name=raw_name,
            name=raw_name,
            value=value,
            section_number=section_number,
            section_name=section_name,
            section_characteristics=section_characteristics,
            storage_class=storage_class,
            aux_symbol_count=aux_count,
            thread_local=thread_local,
        ))
    if table_entries != symbol_count:
        raise ValueError(
            f"symbol listing accounts for {table_entries} table entries, "
            f"header declares {symbol_count}")
    return tuple(symbols)


def _dbghelp_demangle_coff_names(
        inputs: tuple[str, ...]
) -> tuple[tuple[str, ...] | None, tuple[str, ...]]:
    """Demangle Microsoft data names through the Windows DbgHelp API."""
    if sys.platform != "win32":
        return None, ("llvm-undname unavailable and DbgHelp is not available "
                      "off Windows",)
    try:
        library = ctypes.WinDLL("dbghelp", use_last_error=True)
        undecorate = library.UnDecorateSymbolName
        undecorate.argtypes = (
            ctypes.c_char_p, ctypes.c_char_p,
            ctypes.c_ulong, ctypes.c_ulong)
        undecorate.restype = ctypes.c_ulong
    except (AttributeError, OSError) as exc:
        return None, (f"unable to load DbgHelp UnDecorateSymbolName: {exc}",)

    # UNDNAME_NAME_ONLY returns exactly [scope::]name. This is the ownership
    # view the audit needs and cannot be spoofed by a variable type that merely
    # mentions an audited namespace.
    undname_name_only = 0x1000
    names: list[str] = []
    for raw_name in inputs:
        try:
            encoded = raw_name.encode("ascii")
        except UnicodeError as exc:
            return None, (f"non-ASCII Microsoft decorated name: {exc}",)
        decoded = None
        for capacity in (4096, 16384, 65536, 262144):
            output = ctypes.create_string_buffer(capacity)
            ctypes.set_last_error(0)
            length = undecorate(encoded, output, capacity,
                                undname_name_only)
            if length:
                try:
                    decoded = output.value.decode("utf-8")
                except UnicodeError as exc:
                    return None, (
                        f"DbgHelp returned non-UTF-8 symbol text: {exc}",)
                break
            if ctypes.get_last_error() not in (0, 122):
                break
        if (not decoded or decoded == raw_name or
                decoded.startswith("?")):
            return None, (
                f"DbgHelp could not undecorate COFF name {raw_name!r}",)
        names.append(decoded.replace("`anonymous namespace'",
                                     "(anonymous namespace)"))
    return tuple(names), ()


def _demangle_coff_symbols(
        undname: str | None, symbols: tuple[CoffSymbol, ...]
) -> tuple[tuple[CoffSymbol, ...] | None, tuple[str, ...]]:
    indexes: list[int] = []
    inputs: list[str] = []
    suffixes: list[str] = []
    promoted = re.compile(r"(?P<base>.+?)(?P<suffix>(?:\.llvm\.[0-9]+)+)$")
    for index, symbol in enumerate(symbols):
        if (symbol.section_number <= 0 or
                not (symbol.section_characteristics &
                     _COFF_IMAGE_SCN_MEM_WRITE) or
                symbol.thread_local or
                not symbol.raw_name.startswith("?")):
            continue
        match = promoted.fullmatch(symbol.raw_name)
        indexes.append(index)
        inputs.append(match.group("base") if match else symbol.raw_name)
        suffixes.append(match.group("suffix") if match else "")
    if not indexes:
        return tuple(symbols), ()

    if undname is None:
        undecorated, diagnostics = _dbghelp_demangle_coff_names(tuple(inputs))
        if undecorated is None:
            return None, diagnostics
        names = [name + suffix
                 for name, suffix in zip(undecorated, suffixes)]
    else:
        try:
            process = subprocess.run(
                [undname, "--no-variable-type", "--no-access-specifier",
                 "--no-member-type", "--warn-trailing"],
                input="\n".join(inputs) + "\n",
                capture_output=True,
                text=True,
                check=False,
            )
        except (OSError, UnicodeError) as exc:
            return None, (f"unable to execute {undname}: {exc}",)
        if process.returncode != 0:
            return None, (_nm_failure(undname, "Microsoft demangle", process),)
        if process.stderr.strip():
            return None, (
                f"{undname} emitted a diagnostic while demangling COFF "
                f"names: {process.stderr.strip().splitlines()[0][:160]}",)

        groups = process.stdout.split("\n\n")
        if groups and not groups[-1]:
            groups.pop()
        if len(groups) != len(inputs):
            return None, (
                f"{undname} returned {len(groups)} groups for "
                f"{len(inputs)} COFF symbols",)
        names = []
        for expected, suffix, group in zip(inputs, suffixes, groups):
            pair = group.splitlines()
            if (len(pair) != 2 or pair[0] != expected or not pair[1] or
                    pair[1] == expected or pair[1].startswith("?")):
                return None, (f"{undname} returned malformed COFF output",)
            name = pair[1].replace("`anonymous namespace'",
                                  "(anonymous namespace)")
            names.append(name + suffix)

    result = list(symbols)
    for index, name in zip(indexes, names):
        symbol = result[index]
        result[index] = CoffSymbol(
            raw_name=symbol.raw_name,
            name=name,
            value=symbol.value,
            section_number=symbol.section_number,
            section_name=symbol.section_name,
            section_characteristics=symbol.section_characteristics,
            storage_class=symbol.storage_class,
            aux_symbol_count=symbol.aux_symbol_count,
            thread_local=symbol.thread_local,
        )
    return tuple(result), ()


def _read_coff_symbols(readobj: str, undname: str | None,
                       binary: pathlib.Path) -> CoffReadResult:
    try:
        process = subprocess.run(
            [readobj, "--file-header", "--sections", "--symbols",
             "--coff-tls-directory", "--no-demangle", str(binary)],
            capture_output=True,
            text=True,
            check=False,
        )
    except (OSError, UnicodeError) as exc:
        return CoffReadResult(
            NmReadStatus.ERROR,
            diagnostics=(f"unable to execute {readobj}: {exc}",))
    if process.returncode != 0:
        return CoffReadResult(
            NmReadStatus.ERROR,
            diagnostics=(_nm_failure(readobj, "PE/COFF", process),))
    if process.stderr.strip():
        return CoffReadResult(
            NmReadStatus.ERROR,
            diagnostics=(
                f"{readobj} emitted a diagnostic despite exit 0: "
                f"{process.stderr.strip().splitlines()[0][:160]}",))
    try:
        symbols = _parse_coff_readobj(process.stdout)
    except ValueError as exc:
        return CoffReadResult(
            NmReadStatus.ERROR,
            diagnostics=(f"{readobj} PE/COFF output was unsafe: {exc}",))
    demangled, diagnostics = _demangle_coff_symbols(undname, symbols)
    if demangled is None:
        return CoffReadResult(NmReadStatus.ERROR,
                              diagnostics=diagnostics)
    return CoffReadResult(NmReadStatus.OK, demangled)


def _coff_symbol_table_is_auditable(symbols: tuple[CoffSymbol, ...]) -> bool:
    """Require exact writable external and local-static artifact sentinels.

    ``_parse_coff_readobj`` has already accounted for every primary and
    auxiliary record promised by ImageFileHeader.SymbolCount. These two
    different storage classes then prove the expected image was read and that
    the table is not merely an export list with local records stripped. This is
    evidence about the supplied table, not permission for a linker to emit a
    selectively pruned table; the conformance build must still contract to
    retain every live COFF data symbol.
    """
    def candidates(marker: str, storage_class: int) -> list[CoffSymbol]:
        return [
            symbol for symbol in symbols
            if matches_llvm_promoted_symbol(symbol.name, marker) and
            symbol.storage_class == storage_class and
            symbol.section_number > 0 and
            bool(symbol.section_characteristics &
                 _COFF_IMAGE_SCN_MEM_WRITE) and
            not symbol.thread_local
        ]

    external = candidates(_BINARY_AUDIT_EXTERNAL_SENTINEL,
                          _COFF_IMAGE_SYM_CLASS_EXTERNAL)
    local = candidates(_BINARY_AUDIT_LOCAL_SENTINEL,
                       _COFF_IMAGE_SYM_CLASS_STATIC)
    return len(external) == 1 and len(local) == 1


def _coff_audit_view(
        symbols: tuple[CoffSymbol, ...]
) -> tuple[tuple[str, str, str], ...]:
    rows = []
    for symbol in symbols:
        if (symbol.section_number <= 0 or
                not (symbol.section_characteristics &
                     _COFF_IMAGE_SCN_MEM_WRITE) or
                symbol.thread_local):
            continue
        sym_class = ("d" if symbol.storage_class ==
                     _COFF_IMAGE_SYM_CLASS_STATIC else "D")
        section = (f"COFF:{symbol.section_name}:"
                   f"0x{symbol.section_characteristics:08x}")
        rows.append((symbol.name, sym_class, section))
    return tuple(rows)


def _coff_allowlist_view(allowlist: dict) -> dict:
    """Exclude function-local minima whose MSVC owner is not prefix-stable.

    ``llvm-undname`` renders a function-local variable as a quoted function
    declaration. Return/parameter types can themselves mention an audited
    namespace, so substring ownership would be unsafe. The source-provenance
    layer deliberately retains every function-local static when a binary
    backstop is enabled; only prefix-stable namespace/class objects are
    delegated to this PE view.
    """
    result = dict(allowlist)
    result["binary_symbols"] = [
        entry for entry in allowlist.get("binary_symbols", [])
        if not (isinstance(entry.get("symbol"), str) and
                "()::" in entry["symbol"])
    ]
    return result


def symbol_table_is_auditable(
        symbols, section_protections: dict[str, bool] | None = None) -> bool:
    """Require both artifact identity and local-nlist completeness evidence.

    The external root-generation token proves this is the expected artifact,
    but on Mach-O dyld export information can synthesize it after the local
    nlist was stripped. A stable non-external plugin token proves local symbols
    were present too.
    """
    external = any(
        matches_llvm_promoted_symbol(
            name, _BINARY_AUDIT_EXTERNAL_SENTINEL) and
        symbol_is_writable(
            sym_class, section,
            runtime_read_only=_runtime_read_only(
                section, section_protections))
        for name, sym_class, section in symbols)
    local = any(
        matches_llvm_promoted_symbol(name, _BINARY_AUDIT_LOCAL_SENTINEL) and
        sym_class.islower() and symbol_is_writable(
            sym_class, section,
            runtime_read_only=_runtime_read_only(
                section, section_protections))
        for name, sym_class, section in symbols)
    return external and local


def scan_binary(report: Report, build_dir: pathlib.Path,
                allowlist: dict) -> str | None:
    binary = locate_compiler(build_dir)
    if binary is None:
        # Passing --build-dir asks for this scan, so a missing compiler is a
        # wrong path or an unbuilt tree, not a host that cannot run the scan.
        # Reporting it keeps a mistyped path from reading as a clean run.
        names = "|".join(_COMPILER_NAMES)
        report.unscannable.append(
            f"no compiler at {build_dir / 'bin'}/({names}); "
            "build it or correct --build-dir")
        return
    image_format, format_error = binary_image_format(binary)
    if image_format is None or image_format == "unknown":
        report.unscannable.append(format_error or
                                  f"unable to classify {binary}")
        return
    if image_format == "macho-fat":
        report.unscannable.append(
            f"fat Mach-O {binary} requires per-architecture static-symbol "
            "sentinels; aggregate scanning is unsafe")
        return
    if image_format == "pe":
        readobj = shutil.which("llvm-readobj")
        undname = shutil.which("llvm-undname")
        if readobj is None or (undname is None and sys.platform != "win32"):
            missing = ("llvm-readobj" if readobj is None else
                       "llvm-undname (DbgHelp fallback unavailable)")
            report.unscannable.append(
                f"{missing} unavailable for requested PE/COFF scan of "
                f"{binary}")
            return
        result = _read_coff_symbols(readobj, undname, binary)
        if result.status is not NmReadStatus.OK:
            detail = "; ".join(result.diagnostics) or "no diagnostic"
            report.unscannable.append(
                f"PE/COFF symbol audit of {binary} failed: {detail}")
            return
        if not _coff_symbol_table_is_auditable(result.symbols):
            report.unscannable.append(
                f"{readobj} returned no unique writable external/static "
                "COFF symbol sentinels")
            return
        report.binary.extend(
            audit_symbols(_coff_audit_view(result.symbols),
                          _coff_allowlist_view(allowlist)))
        return "pe"
    candidates: list[str] = []
    for name in ("llvm-nm", "nm"):
        candidate = shutil.which(name)
        if candidate is not None and candidate not in candidates:
            candidates.append(candidate)
    if not candidates:
        report.unscannable.append(
            f"nm/llvm-nm unavailable for requested binary scan of {binary}")
        return
    diagnostics: list[str] = []
    section_protections = artifact_section_protections(binary, image_format)
    tls_sections = artifact_tls_sections(binary, image_format)
    for nm in candidates:
        result = _nm_symbols(nm, binary,
                             mach_o=image_format == "macho-thin")
        if result.status is NmReadStatus.OK:
            if not symbol_table_is_auditable(result.symbols,
                                              section_protections):
                diagnostics.append(
                    f"{nm} returned no complete writable external/local "
                    "static-symbol sentinels")
                continue
            report.binary.extend(audit_symbols(
                result.symbols, allowlist, section_protections,
                tls_sections))
            return image_format
        diagnostics.extend(result.diagnostics)
    detail = "; ".join(diagnostics) or "no candidate returned a result"
    report.unscannable.append(
        f"installed nm tools could not read {binary}: {detail}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir", type=pathlib.Path, default=None,
        help="scan the linked compiler and enable authoritative release-mode "
             "owner coverage; without this, strict is a conservative "
             "source-only preflight")
    parser.add_argument("--strict", action="store_true",
                        help="fail on advisory heuristic findings too")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    allowlist = load_allowlist()
    report = Report()
    validate_allowlist(report, allowlist)
    scan_sources(report, allowlist)
    scan_headers(report, allowlist)
    binary_coverage = None
    if args.build_dir is not None:
        binary_coverage = scan_binary(report, args.build_dir, allowlist)
    scan_translation_unit_state(
        report, allowlist, binary_backstop=binary_coverage is not None,
        preserve_function_locals=binary_coverage == "pe")

    if args.json:
        print(json.dumps({
            "forbidden": report.forbidden,
            "heuristic": report.heuristic,
            "headers": report.headers,
            "source_globals": report.source_globals,
            "binary": report.binary,
            "unscannable": report.unscannable,
            "failed": report.failed(args.strict),
        }, indent=2))
    else:
        if report.forbidden:
            print("== forbidden process-global state (must be zero) ==")
            for item in report.forbidden:
                print(f"error: {item}", file=sys.stderr)
        if report.headers:
            print("== header state with internal linkage (one copy per "
                  "translation unit) ==")
            for item in report.headers:
                print(f"error: {item}", file=sys.stderr)
        if report.source_globals:
            print("== unqualified mutable state in audited implementation "
                  "files ==")
            for item in report.source_globals:
                print(f"error: {item}", file=sys.stderr)
        if report.unscannable:
            print("== requested audit could not run ==")
            for item in report.unscannable:
                print(f"error: {item}", file=sys.stderr)
        if report.binary:
            print("== forbidden writable NeverC/linker symbols ==")
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

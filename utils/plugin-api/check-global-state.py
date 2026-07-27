#!/usr/bin/env python3
"""Audit the plugin/dyncode/linker trees for plugin-related process-global
mutable compilation state.

This is the first-release hard gate for the design requirement that "frontend,
LTO, linker and dyncode carry no plugin-related process-global mutable
compilation state".

Three layers of checking:

1. FORBIDDEN symbols -- a precise, zero-tolerance list of known process-global
   escape hatches (removed prototype loader, dyncode current-options/mode
   singletons, the three NeverC ``ListRegister*Callbacks`` vectors, linker
   ``parallel::strategy``/``getThreadIndex`` dependence and the old
   ``CommonLinkerContext::destroy``/``lctx`` singleton).  Any hit in runtime
   code (comments and string literals are stripped first) is a hard failure.

2. Heuristic declaration scan -- reports ``thread_local`` storage declared in
   the plugin/dyncode/linker trees.  Entries documented in
   ``global-state-allowlist.json`` (with owner, lifetime and justification)
   are accepted; anything else is reported.  This is the only layer that judges
   thread-local storage, because it can pin a declaration to a file, an owner
   and a clearing test -- none of which a symbol name carries.  It fails the
   build under ``--strict``, which the workflows pass.  File-scope mutable
   statics are deliberately left to the binary layer below: a source-level
   regex cannot tell a mutable global from a constant-initialized table,
   whereas the section a symbol lands in can.

3. Header internal-linkage scan -- a hard failure for mutable state defined in
   a header with internal linkage (an anonymous namespace, or ``static`` at
   namespace scope, including the ``inline static`` spelling where ``static``
   silently wins).  Such an object exists once per *including translation
   unit*, so a flag set through one includer reads unset through the next.
   This tree is a header-only port of LLVM, so state upstream keeps in a .cpp
   -- where an anonymous namespace is exactly right -- now sits in headers the
   plugin path includes, where it means the opposite.  The binary layer below
   cannot see this: such symbols demangle to a bare ``(anonymous
   namespace)::X`` with no ``neverc::`` scope to match on, and two copies of
   one object are indistinguishable from two same-named objects in different
   .cpp files.  Constant-initialized data is exempt: every copy holds the same
   bytes and nothing can write to them.

Optionally, ``--build-dir`` scans the writable data symbols of the linked
compiler (``<build-dir>/bin/neverc``) via ``nm``/``llvm-nm``.  This is the
layer that catches file-scope mutable statics, and it is a hard failure.  It
skips itself only where it genuinely cannot run -- no ``nm``, or a PE image it
cannot classify -- and says which.  A missing compiler is instead reported,
because ``--build-dir`` is a request for this scan, and a mistyped path that
prints "skipping" is indistinguishable from a clean run.  Thread-local symbols
are excluded here and left to layer 2: TLS is per-thread rather than
process-global, and its symbol layout differs enough between platforms that
judging it here made the gate reach opposite verdicts on ELF and Mach-O for
the same source.
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

# Headers scanned for mutable state that has *internal linkage*, which gives
# every including translation unit its own copy.  ``llvm/include`` is in scope
# because this tree is a header-only port of LLVM: state that upstream keeps in
# a .cpp now sits in headers the plugin path includes, so a split there is
# plugin-visible process state.  See ``scan_headers``.
HEADER_AUDIT_DIRS = (
    "llvm/include",
    "neverc/include/neverc/Plugin",
    "neverc/include/neverc/DynCode",
    "neverc/include/neverc/Linker",
)


@dataclass
class Report:
    forbidden: list[str] = field(default_factory=list)
    heuristic: list[str] = field(default_factory=list)
    headers: list[str] = field(default_factory=list)
    binary: list[str] = field(default_factory=list)
    # Reasons the binary scan was asked for but could not run.  Kept apart from
    # `binary` so "the tree is not built" never reads as "a symbol is bad".
    unscannable: list[str] = field(default_factory=list)

    def failed(self, strict: bool) -> bool:
        if self.forbidden or self.headers or self.binary or self.unscannable:
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
        # A raw string may open on its encoding prefix, so only consider one
        # when the preceding character cannot be part of an identifier.
        if c in "RLuU" and (i == 0 or not (text[i - 1].isalnum()
                                           or text[i - 1] == "_")):
            end = raw_string_end(text, i)
            if end is not None:
                out.append('""')
                out.append("\n" * text.count("\n", i, end))
                i = end
                continue
        if c == "'" and is_digit_separator(text, i):
            out.append(c)
            i += 1
            continue
        if c in ('"', "'"):
            quote = c
            out.append(quote)
            i += 1
            while i < n:
                if text[i] == "\\":
                    # A line continuation consumes a real newline; keep it so
                    # the line numbers this scan reports stay usable.
                    if text[i + 1:i + 2] == "\n":
                        out.append("\n")
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
    """Match a writable symbol against the ``binary_symbols`` list.

    Only address-only pass/type identity tokens, const objects whose vtable
    pointer forces a relocation, and sync primitives without compilation state
    are eligible; each entry carries an owner and justification.  A lookup or
    interface table is not: making it ``constexpr`` moves it to read-only
    storage, which is a fix rather than a documented exception.  Thread-local
    symbols never reach here -- see ``symbol_is_thread_local``.
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


_OPEN_NAMESPACE = re.compile(r"\bnamespace\b(?P<name>[\w\s:]*)$")
_OPEN_RECORD = re.compile(r"\b(?:struct|class|union|enum)\b[^;=]*$")
_HAS_STATIC = re.compile(r"(?:^|\s)static(?:\s|$)")
_ENDS_IN_IDENTIFIER = re.compile(r"[A-Za-z_]\w*\s*(?:\[[^\]]*\])?\s*$")
_LEADING_CONST = re.compile(r"^(?:\w+\s+)*?(?:const|constexpr)\s")
_NOT_A_DEFINITION = re.compile(
    r"^\s*(?:template|using|typedef|friend|return|constexpr|extern|"
    r"static_assert|struct\s|class\s|union\s|enum\s|namespace\s)")
# Types defined in an anonymous namespace, which is what makes a variable of
# that type internally linked no matter how the variable itself is spelled.
_ANON_TYPE = re.compile(r"\b(?:struct|class|union|enum)\s+(\w+)\b")


def is_read_only(declarator: str) -> bool:
    """Report whether the declared object itself cannot be written.

    Constant-initialized read-only data is duplicated harmlessly: every copy
    holds the same bytes.  A pointer is the trap -- in ``const char *Msg`` the
    ``const`` qualifies the characters, not ``Msg``, so the pointer is still
    assignable and a per-includer copy still diverges.  Only ``* const`` makes
    the pointer itself immutable.
    """
    if re.search(r"\bconstexpr\b", declarator):
        return True             # a constexpr variable is a const object
    star = declarator.rfind("*")
    if star >= 0:
        return re.search(r"\bconst\b", declarator[star:]) is not None
    return _LEADING_CONST.match(declarator) is not None


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
            if head:
                # Surface the head too: a type definition is terminated by the
                # brace, not by a ';' at this level, so a caller looking for
                # `struct X` would otherwise never see it.
                yield stmt_start, list(scopes), text[stmt_start:i]
            match = _OPEN_NAMESPACE.search(head)
            if match:
                scopes.append("ns" if match.group("name").strip() else "anon")
            elif _OPEN_RECORD.search(head):
                scopes.append("record")
            elif "anon" in scopes or (_HAS_STATIC.search(head)
                                      and scopes[-1:] != ["record"]):
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
    code = strip_preprocessor(strip_comments_and_strings(text))
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
        if not flat or len(flat.split()) < 2 or _NOT_A_DEFINITION.match(flat):
            continue
        head = flat.replace("=", " = ").split("=")[0].strip()
        if not _ENDS_IN_IDENTIFIER.search(head):
            continue
        innermost = scopes[-1] if scopes else "ns"
        if innermost == "record":
            continue                    # a member declaration, not a definition
        if innermost == "block":
            continue                    # a local of a mergeable function
        if innermost == "block-internal":
            if not _HAS_STATIC.search(flat):
                continue                # an ordinary local
            kind = "function-local static of an unmergeable function"
        else:
            if "(" in flat:
                continue                # a function declaration, not an object
            if is_read_only(head):
                continue
            borrowed = sorted(t for t in anon_types
                              if re.search(rf"\b{re.escape(t)}\b", head))
            if "anon" in scopes:
                kind = "anonymous-namespace object"
            elif _HAS_STATIC.search(flat):
                kind = "file-scope static"
            elif borrowed:
                kind = ("object of anonymous-namespace type "
                        f"`{borrowed[0]}`, so internally linked despite "
                        "`inline`")
            else:
                continue                # external linkage: one per program
        line = code.count("\n", 0, offset + len(stmt) - len(stmt.lstrip())) + 1
        yield line, kind, flat


def scan_headers(report: Report, allowlist: dict) -> None:
    for base in (ROOT / d for d in HEADER_AUDIT_DIRS):
        if not base.exists():
            continue
        for path in sorted(base.rglob("*.h")):
            rel = str(path.relative_to(ROOT))
            text = path.read_text(encoding="utf-8", errors="replace")
            for line, kind, stmt in header_internal_state(text):
                if allowlisted(rel, stmt, allowlist):
                    continue
                report.headers.append(
                    f"{rel}:{line}: {kind} `{stmt[:80]}`")


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
#
# Known limit: matching on the namespace means an anonymous namespace opened at
# global scope is invisible here, since its members demangle to a bare
# ``(anonymous namespace)::X``.  Three audited files do that
# (IRPluginInterfaces.cpp, CallingConventionPlan.cpp, PluginABILowering.cpp);
# all three currently hold only constexpr data and functions.  Nesting an
# anonymous namespace inside ``neverc::plugin``, as the other 198 audited files
# do, is what keeps its contents in view of this scan.
_BINARY_SCOPES = ("neverc::plugin", "neverc::dyncode")
# Some symbols reach us still mangled: nm's demangler gives up on Mach-O
# decorations such as the ``$tlv$init`` suffix and returns the raw Itanium
# spelling.  Matching those too keeps a symbol from slipping through merely
# because it could not be demangled.
_BINARY_SCOPES_MANGLED = ("6neverc6plugin", "6neverc7dyncode")
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
# The same categories for names that stayed mangled, where the English
# spellings above never appear: vtable, typeinfo, typeinfo name, guard variable.
_BINARY_NOISE_MANGLED = ("_ZTV", "_ZTI", "_ZTS", "_ZGV")

# Names the linked compiler can have.  Windows builds produce `neverc.exe`, and
# looking only for the extensionless spelling used to make the scan report a
# missing binary there -- indistinguishable from a mistyped --build-dir.
_COMPILER_NAMES = ("neverc", "neverc.exe")

# nm's one-letter class is only a reliable writability signal on Mach-O.  On
# ELF, ``.data.rel.ro`` is a SHF_WRITE section and therefore reported as `d`,
# even though the dynamic loader maps it read-only once relocations are applied
# (RELRO).  Every constant table holding a function pointer lands there, so
# classifying by letter alone would flag all of them.  Nothing dynamically
# initialized can live in ``.data.rel.ro`` -- the initializer would run after
# the segment is already read-only -- so such objects go to ``.data``/``.bss``
# and are still caught below.
_WRITABLE_CLASSES = ("d", "D", "b", "B", "g", "G")
_WRITABLE_ELF_PREFIXES = (".data", ".bss")

# Thread-local storage, which this layer skips (see the module docstring).  ELF
# keeps a TLS object in ``.tbss``/``.tdata``; Mach-O splits it into a
# ``__thread_vars`` descriptor plus a ``$tlv$init`` storage symbol whose suffix
# also defeats the demangler.  Both spellings are recognised so the exclusion
# does not depend on which nm produced the listing.
_TLS_ELF_PREFIXES = (".tbss", ".tdata")
_TLS_MACHO_SECTIONS = ("__DATA,__thread_vars", "__DATA,__thread_bss",
                       "__DATA,__thread_data")
_TLS_MACHO_MARKER = "$tlv$"


def symbol_is_thread_local(name: str, section: str) -> bool:
    """Report whether *name* denotes thread-local rather than process storage."""
    if _TLS_MACHO_MARKER in name:
        return True
    if section in _TLS_MACHO_SECTIONS:
        return True
    return any(
        section == prefix or section.startswith(prefix + ".")
        for prefix in _TLS_ELF_PREFIXES
    )


def symbol_in_audited_scope(name: str) -> bool:
    """Report whether *name* lives in an audited namespace, demangled or not."""
    return (any(scope in name for scope in _BINARY_SCOPES)
            or any(scope in name for scope in _BINARY_SCOPES_MANGLED))


def symbol_is_compiler_noise(name: str) -> bool:
    """Report whether *name* is compiler-emitted data rather than plugin state."""
    low = name.lower()
    if any(noise in low for noise in _BINARY_NOISE):
        return True
    return any(marker in name for marker in _BINARY_NOISE_MANGLED)


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


def audit_symbols(symbols, allowlist: dict) -> list[str]:
    """Return findings for the writable, in-scope symbols of an nm listing.

    Kept separate from :func:`scan_binary` so the classification can be checked
    against synthetic ELF and Mach-O listings -- the two disagreed once, and a
    test only catches that if it can supply both without a linked compiler.
    """
    findings: list[str] = []
    seen: set[str] = set()
    for name, sym_class, section in symbols:
        if symbol_is_thread_local(name, section):
            continue
        if not symbol_is_writable(sym_class, section):
            continue
        if not symbol_in_audited_scope(name):
            continue
        if symbol_is_compiler_noise(name):
            continue
        if name in seen:
            continue
        seen.add(name)
        if allowlisted_binary_symbol(name, allowlist):
            continue
        where = section or sym_class
        findings.append(f"neverc: writable symbol `{name}` ({where})")
    return findings


def locate_compiler(build_dir: pathlib.Path) -> pathlib.Path | None:
    """Return the linked compiler under *build_dir*, whatever the host names it."""
    for name in _COMPILER_NAMES:
        candidate = build_dir / "bin" / name
        if candidate.exists():
            return candidate
    return None


def scan_binary(report: Report, build_dir: pathlib.Path, allowlist: dict) -> None:
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
    if binary.suffix == ".exe":
        # A PE image would need COFF sections and Microsoft mangling, which the
        # classification below does not speak; on Windows the source scan under
        # --strict is what carries the gate.
        print(f"note: {binary.name} is a PE image, which this layer cannot "
              "classify; skipping binary symbol scan")
        return
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if nm is None:
        print("note: nm/llvm-nm unavailable; skipping binary symbol scan")
        return
    symbols = _nm_symbols(nm, binary)
    if symbols is None:
        print(f"note: unable to run {nm} on {binary}; skipping binary scan")
        return
    report.binary.extend(audit_symbols(symbols, allowlist))


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
    scan_headers(report, allowlist)
    if args.build_dir is not None:
        scan_binary(report, args.build_dir, allowlist)

    if args.json:
        print(json.dumps({
            "forbidden": report.forbidden,
            "heuristic": report.heuristic,
            "headers": report.headers,
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
        if report.unscannable:
            print("== binary scan requested but could not run ==")
            for item in report.unscannable:
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

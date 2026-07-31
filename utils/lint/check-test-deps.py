#!/usr/bin/env python3
"""Validate standard-library registration and test dependency coverage.

Checks that:
* every manifest symbol is defined by the source file that registers it;
* every manifest dependency names an existing module;
* each STD_TEST() source list covers the transitive manifest dependencies;
* every function declared by a public network header has a source definition
  and a manifest registration.

The checks are deliberately source-only so they can run in lint jobs without
building the compiler or any target binaries.
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MANIFEST = REPO / "std" / "manifest.json"
TESTS_CPP = REPO / "tests" / "neverc" / "StdLibTests.cpp"


def load_manifest():
    with open(MANIFEST) as f:
        return json.load(f)


def module_source_files(mod):
    """Return the set of source file paths declared in a module's symbols."""
    return set(mod.get("symbols", {}).values())


def strip_c_comments_and_literals(text):
    """Replace C comments and literal contents while preserving line breaks."""
    out = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if state == "code":
            if ch == "/" and nxt == "/":
                out.extend("  ")
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                out.extend("  ")
                i += 2
                state = "block_comment"
                continue
            if ch == '"':
                out.append(" ")
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(" ")
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue

        if state == "line_comment":
            if ch == "\n":
                out.append("\n")
                state = "code"
            else:
                out.append(" ")
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                out.extend("  ")
                i += 2
                state = "code"
            else:
                out.append("\n" if ch == "\n" else " ")
                i += 1
            continue

        if ch == "\\" and nxt:
            out.extend("  ")
            i += 2
        elif (state == "string" and ch == '"') or (
            state == "char" and ch == "'"
        ):
            out.append(" ")
            i += 1
            state = "code"
        else:
            out.append("\n" if ch == "\n" else " ")
            i += 1

    return "".join(out)


def c_function_symbols(text, terminator):
    """Find C function names ending in ``terminator`` ('{' or ';').

    Match declaration-shaped line prefixes instead of tracking brace depth:
    mutually exclusive preprocessor branches commonly contain two textual
    function openings but only one closing brace.
    """
    text = strip_c_comments_and_literals(text)
    symbols = set()
    candidate = re.compile(
        r"(?m)^[ \t]*(?!#)"
        r"(?P<prefix>(?:[A-Za-z_][A-Za-z0-9_]*[ \t*]+)*)"
        r"(?P<name>neverc_[A-Za-z0-9_]+)[ \t\r\n]*\("
    )

    for match in candidate.finditer(text):
        name = match.group("name")
        if terminator == "{" and re.search(
            r"\bstatic\b", match.group("prefix")
        ):
            continue
        pos = match.end() - 1
        depth = 0
        while pos < len(text):
            if text[pos] == "(":
                depth += 1
            elif text[pos] == ")":
                depth -= 1
                if depth == 0:
                    pos += 1
                    break
            pos += 1
        if depth != 0:
            continue

        suffix_parens = 0
        suffix_brackets = 0
        while pos < len(text):
            suffix = text[pos]
            if suffix == "(":
                suffix_parens += 1
            elif suffix == ")":
                suffix_parens = max(0, suffix_parens - 1)
            elif suffix == "[":
                suffix_brackets += 1
            elif suffix == "]":
                suffix_brackets = max(0, suffix_brackets - 1)
            elif (
                suffix in "{;"
                and suffix_parens == 0
                and suffix_brackets == 0
            ):
                if suffix == terminator:
                    symbols.add(name)
                break
            pos += 1

    return symbols


def source_definitions(path, cache):
    if path not in cache:
        cache[path] = c_function_symbols(path.read_text(), "{")
    return cache[path]


def load_function_aliases():
    """Load simple object-like function aliases used by public headers."""
    aliases = {}
    define = re.compile(
        r"(?m)^[ \t]*#[ \t]*define[ \t]+"
        r"(neverc_[A-Za-z0-9_]+)[ \t]+"
        r"(neverc_[A-Za-z0-9_]+)[ \t]*(?:$|//|/\*)"
    )
    for header_path in (REPO / "std" / "include").rglob("*.h"):
        for alias, target in define.findall(header_path.read_text()):
            aliases[alias] = target
    return aliases


def preprocessed_definitions(definitions, aliases):
    """Resolve macro-spelled source definitions to emitted symbol names."""
    resolved = set()
    for definition in definitions:
        symbol = definition
        seen = set()
        while symbol in aliases and symbol not in seen:
            seen.add(symbol)
            symbol = aliases[symbol]
        resolved.add(symbol)
    return resolved


def is_network_module(module_key):
    return module_key == "http" or module_key.startswith("net/")


def dependency_source_files(module_key, modules, errors):
    """Return source files from the complete dependency closure."""
    result = set()
    seen = {module_key}
    pending = list(modules[module_key].get("deps", []))

    while pending:
        dep_key = pending.pop()
        if dep_key in seen:
            continue
        seen.add(dep_key)
        dep_mod = modules.get(dep_key)
        if not dep_mod:
            errors.append(
                f"  manifest: module '{module_key}' depends on missing "
                f"module '{dep_key}'"
            )
            continue
        result.update(module_source_files(dep_mod))
        pending.extend(dep_mod.get("deps", []))

    return result


def parse_tests(text):
    """Extract (test_name, set_of_source_paths) from STD_TEST() calls.

    Handles the HTTP_TLS_DEPS macro by pre-expanding it inline.
    """
    macro_match = re.search(
        r"#define\s+HTTP_TLS_DEPS\s*\\\n((?:.*\\\n)*.*?\n)", text
    )
    tls_deps_files = set()
    if macro_match:
        macro_body = macro_match.group(1).replace("\\\n", " ")
        tls_deps_files = set(re.findall(r'"([^"]+)"', macro_body))

    expanded = text.replace("HTTP_TLS_DEPS", ", ".join(
        f'"{f}"' for f in sorted(tls_deps_files)
    ))

    tests = {}
    for m in re.finditer(
        r'STD_TEST\s*\(\s*(\w+)\s*(?:,\s*((?:[^()]*|\((?:[^()]*|\([^()]*\))*\))*))?\)',
        expanded,
    ):
        name = m.group(1)
        args = m.group(2) or ""
        files = set(re.findall(r'"([^"]+)"', args))
        tests[name] = files
    return tests


def normalise_test_name(name):
    """Map test name variants to manifest module keys."""
    mapping = {
        "tcp": "net/tcp",
        "udp": "net/udp",
        "websocket": "net/websocket",
        "httptest": "net/httptest",
        "http2": "net/http2",
        "url": "net/url",
        "netip": "net/netip",
        "mail": "net/mail",
        "textproto": "net/textproto",
        "resolve": "net/resolve",
        "net_interface": "net/interface",
        "tls": "crypto/tls",
        "cookiejar": "net/cookiejar",
    }
    return mapping.get(name, name)


def main():
    manifest = load_manifest()
    modules = manifest["modules"]

    text = TESTS_CPP.read_text()
    tests = parse_tests(text)

    errors = []
    definition_cache = {}
    function_aliases = load_function_aliases()

    for module_key, mod in sorted(modules.items()):
        if is_network_module(module_key):
            header = mod.get("header")
            if not header:
                errors.append(
                    f"  {module_key}: public network module has no header"
                )
            elif not (REPO / "std" / header).is_file():
                errors.append(
                    f"  {module_key}: header '{header}' does not exist"
                )

        symbols = mod.get("symbols", {})
        for method, symbol in sorted(mod.get("dot_methods", {}).items()):
            if symbol not in symbols:
                errors.append(
                    f"  {module_key}: dot method '{method}' targets "
                    f"unregistered symbol '{symbol}'"
                )

        for symbol, source in sorted(symbols.items()):
            source_path = REPO / "std" / source
            if not source_path.is_file():
                errors.append(
                    f"  {module_key}: symbol '{symbol}' points to missing "
                    f"file '{source}'"
                )
                continue
            definitions = source_definitions(source_path, definition_cache)
            emitted_definitions = preprocessed_definitions(
                definitions, function_aliases
            )
            if symbol not in emitted_definitions:
                errors.append(
                    f"  {module_key}: symbol '{symbol}' is not defined in "
                    f"'{source}'"
                )

        dependency_source_files(module_key, modules, errors)

    for test_name, test_files in sorted(tests.items()):
        mod_key = normalise_test_name(test_name)
        mod = modules.get(mod_key)
        if not mod or "deps" not in mod:
            continue

        dep_files = dependency_source_files(mod_key, modules, errors)
        missing = dep_files - test_files
        if missing:
            errors.append(
                f"  {test_name}: missing files from transitive deps:\n"
                + "\n".join(f"    - {f}" for f in sorted(missing))
            )

    network_definitions = set()
    registered_symbols = {
        symbol
        for mod in modules.values()
        for symbol in mod.get("symbols", {})
    }
    for source_path in sorted((REPO / "std" / "src" / "net").rglob("*.c")):
        network_definitions.update(
            preprocessed_definitions(
                source_definitions(source_path, definition_cache),
                function_aliases,
            )
        )
    for header_path in sorted(
        (REPO / "std" / "include" / "neverc" / "std" / "net").rglob("*.h")
    ):
        declarations = {
            symbol
            for symbol in c_function_symbols(header_path.read_text(), ";")
            if not symbol.endswith("_t")
        }
        for symbol in sorted(declarations - network_definitions):
            errors.append(
                f"  public network API '{symbol}' from "
                f"'{header_path.relative_to(REPO / 'std')}' has no source "
                "definition"
            )
        for symbol in sorted(declarations - registered_symbols):
            errors.append(
                f"  public network API '{symbol}' from "
                f"'{header_path.relative_to(REPO / 'std')}' is not registered "
                "in manifest.json"
            )

    if errors:
        print("FAIL: standard-library registration validation failed:\n")
        print("\n".join(errors))
        return 1
    else:
        print("OK: manifest symbols, network APIs, and test deps are valid")
        return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate standard-library registration and test dependency coverage.

Checks that:
* every manifest symbol is defined by the source file that registers it;
* every manifest dependency names an existing module;
* each STD_TEST() source list covers the transitive manifest dependencies;
* every standalone std test source is consumed by the test registry or CI;
* every manifest module names an existing public header;
* every function declared by a public standard-library header has a definition
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


def resolve_function_alias(symbol, aliases):
    """Resolve an object-like function alias to its emitted symbol name."""
    seen = set()
    while symbol in aliases and symbol not in seen:
        seen.add(symbol)
        symbol = aliases[symbol]
    return symbol


def preprocessed_definitions(definitions, aliases):
    """Resolve macro-spelled source definitions to emitted symbol names."""
    return {
        resolve_function_alias(definition, aliases)
        for definition in definitions
    }


def is_public_api_symbol(symbol):
    """Reject parser false positives and explicitly test-only TLS hooks."""
    return not symbol.endswith("_t") and not symbol.startswith(
        "neverc_tls_test_"
    )


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

    Pre-expands simple multiline *_DEPS macros used by the test registry.
    """
    macro_pattern = re.compile(
        r"#define\s+([A-Z][A-Z0-9_]*_DEPS)\s*\\\n"
        r"((?:.*\\\n)*.*?\n)"
    )
    dependency_macros = {}
    for macro_match in macro_pattern.finditer(text):
        macro_name = macro_match.group(1)
        macro_body = macro_match.group(2).replace("\\\n", " ")
        dependency_macros[macro_name] = set(
            re.findall(r'"([^"]+)"', macro_body)
        )

    expanded = text
    for macro_name, files in dependency_macros.items():
        replacement = ", ".join(f'"{f}"' for f in sorted(files))
        expanded = re.sub(
            rf"\b{re.escape(macro_name)}\b", replacement, expanded
        )

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


def standalone_test_sources(repo, registry_text):
    """Return standalone std test sources with no known test consumer."""
    test_dir = repo / "tests" / "neverc" / "std"
    if not test_dir.is_dir():
        return []

    registered_names = set(parse_tests(registry_text))
    registered_names.update(
        re.findall(
            r'compileAndRunStdTest\s*\(\s*"([^"]+)"', registry_text
        )
    )
    registered_files = {f"test_{name}.c" for name in registered_names}

    consumer_paths = [repo / "tests" / "neverc" / "StdLibTests.cpp"]
    for root in (repo / "utils" / "ci", repo / ".github" / "workflows"):
        if root.is_dir():
            consumer_paths.extend(
                path for path in root.rglob("*") if path.is_file()
            )
    consumer_text = "\n".join(
        path.read_text(errors="replace")
        for path in consumer_paths
        if path.is_file()
    )

    unconsumed = []
    for path in sorted(test_dir.glob("*.c")):
        if not (path.name.startswith("test_") or path.name.endswith("_test.c")):
            continue
        source = strip_c_comments_and_literals(path.read_text())
        if not re.search(r"(?m)^\s*(?:int|void)\s+main\s*\(", source):
            continue
        if path.name in registered_files or path.name in consumer_text:
            continue
        unconsumed.append(path.relative_to(repo))
    return unconsumed


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
        "scanner": "text/scanner",
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

    for path in standalone_test_sources(REPO, text):
        errors.append(
            f"  {path}: standalone std test has no registry or CI consumer"
        )

    for module_key, mod in sorted(modules.items()):
        header = mod.get("header")
        if not header:
            errors.append(f"  {module_key}: public module has no header")
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

    runtime_definitions = set()
    registered_symbols = {
        symbol
        for mod in modules.values()
        for symbol in mod.get("symbols", {})
    }
    for source_path in sorted((REPO / "std" / "src").rglob("*.c")):
        runtime_definitions.update(
            preprocessed_definitions(
                source_definitions(source_path, definition_cache),
                function_aliases,
            )
        )
    for header_path in sorted(
        (REPO / "std" / "include" / "neverc" / "std").rglob("*.h")
    ):
        declarations = set()
        for declared_symbol in c_function_symbols(
            header_path.read_text(), ";"
        ):
            symbol = resolve_function_alias(
                declared_symbol, function_aliases
            )
            if is_public_api_symbol(declared_symbol) and is_public_api_symbol(
                symbol
            ):
                declarations.add(symbol)

        for symbol in sorted(declarations - runtime_definitions):
            errors.append(
                f"  public std API '{symbol}' from "
                f"'{header_path.relative_to(REPO / 'std')}' has no source "
                "definition"
            )
        for symbol in sorted(declarations - registered_symbols):
            errors.append(
                f"  public std API '{symbol}' from "
                f"'{header_path.relative_to(REPO / 'std')}' is not registered "
                "in manifest.json"
            )

    if errors:
        print("FAIL: standard-library registration validation failed:\n")
        print("\n".join(errors))
        return 1
    else:
        print("OK: manifest symbols, public APIs, and test deps are valid")
        return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate StdLibTests.cpp source lists against manifest.json deps.

For each STD_TEST() call, if the test name maps to a manifest module that
declares deps, verify that all source files from those dep modules are
included (directly or via the HTTP_TLS_DEPS macro) in the test's source
list.  Exits non-zero when gaps are found.
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST = REPO / "std" / "manifest.json"
TESTS_CPP = REPO / "tests" / "neverc" / "StdLibTests.cpp"


def load_manifest():
    with open(MANIFEST) as f:
        return json.load(f)


def module_source_files(mod):
    """Return the set of source file paths declared in a module's symbols."""
    return set(mod.get("symbols", {}).values())


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

    for test_name, test_files in sorted(tests.items()):
        mod_key = normalise_test_name(test_name)
        mod = modules.get(mod_key)
        if not mod or "deps" not in mod:
            continue

        for dep_key in mod["deps"]:
            dep_mod = modules.get(dep_key)
            if not dep_mod:
                errors.append(
                    f"  {test_name}: dep '{dep_key}' not found in manifest"
                )
                continue

            dep_files = module_source_files(dep_mod)
            missing = dep_files - test_files
            if missing:
                errors.append(
                    f"  {test_name}: missing files from dep '{dep_key}':\n"
                    + "\n".join(f"    - {f}" for f in sorted(missing))
                )

    if errors:
        print("FAIL: StdLibTests.cpp is missing dependency source files:\n")
        print("\n".join(errors))
        return 1
    else:
        print("OK: all manifest deps are covered in StdLibTests.cpp")
        return 0


if __name__ == "__main__":
    sys.exit(main())

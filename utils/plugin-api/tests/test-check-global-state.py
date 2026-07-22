#!/usr/bin/env python3
"""Self-test for check-global-state.py.

Guards the fragile parts of the checker so the release gate does not silently
degrade into a single easy-to-miss ``rg``:

* comment/string stripping (a forbidden token in a comment or literal must not
  count, but the same token in real code must);
* forbidden-symbol detection across the plugin/dyncode/linker trees;
* the composite regex spellings (e.g. ``static CommonLinkerContext *lctx``).
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys


HERE = pathlib.Path(__file__).resolve().parent
CHECKER = HERE.parent / "check-global-state.py"


def load_checker():
    spec = importlib.util.spec_from_file_location("check_global_state", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    # Register before exec so @dataclass can resolve the module namespace.
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def expect(cond: bool, message: str, failures: list[str]) -> None:
    if not cond:
        failures.append(message)


def main() -> int:
    mod = load_checker()
    failures: list[str] = []

    strip = mod.strip_comments_and_strings

    # 1. Line comment hides the token.
    code = strip("int x; // getGlobalPluginLoader\n")
    expect("getGlobalPluginLoader" not in code,
           "line-comment token leaked past stripper", failures)

    # 2. Block comment hides the token.
    code = strip("/* getCurrentDynCodeOptions */ int y;\n")
    expect("getCurrentDynCodeOptions" not in code,
           "block-comment token leaked past stripper", failures)

    # 3. String literal hides the token.
    code = strip('const char *s = "setDynCodeModeState";\n')
    expect("setDynCodeModeState" not in code,
           "string-literal token leaked past stripper", failures)

    # 4. Real code keeps the token.
    code = strip("auto &o = currentDynCodeOptionsStorage();\n")
    expect("currentDynCodeOptionsStorage" in code,
           "real-code token wrongly stripped", failures)

    # 5. Newlines preserved so line numbers stay accurate.
    code = strip("a\n/* c\nc */\nb\n")
    expect(code.count("\n") == 4,
           f"line count changed by stripper: {code.count(chr(10))}", failures)

    # 6. Composite forbidden regex spelling.
    matched = any(
        pat.search("static CommonLinkerContext *lctx = nullptr;")
        for pat, _ in mod.FORBIDDEN_REGEXES
    )
    expect(matched, "composite lctx regex did not match", failures)

    # 7. Forbidden symbol table is non-empty and includes the dyncode singleton.
    expect("getCurrentDynCodeOptions" in mod.FORBIDDEN_SYMBOLS,
           "FORBIDDEN_SYMBOLS missing dyncode current-options", failures)
    expect("getGlobalPluginLoader" in mod.FORBIDDEN_SYMBOLS,
           "FORBIDDEN_SYMBOLS missing prototype loader", failures)

    # 8. Allowlist parses and only documents thread_local/static facades.
    allowlist = mod.load_allowlist()
    expect(isinstance(allowlist.get("entries"), list) and allowlist["entries"],
           "allowlist did not load any entries", failures)

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print(f"test-check-global-state: OK ({8} checks passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

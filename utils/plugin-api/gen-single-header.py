#!/usr/bin/env python3

"""Generate the distributed single-header form of the NeverC plugin ABI.

The modular headers under ``neverc/include/neverc/Plugin`` remain the source of
truth. This script inlines the public modules declared in
``plugin-api-modules.json`` (in canonical order) into one self-contained pure-C
header that ships in the SDK, so a plugin can build against a single file with
no side-by-side module headers or schema fragments.

Per-module include guards are preserved so the generated header is also a safe
drop-in for the modular aggregate: including a modular header afterwards becomes
a no-op. Project includes are dropped (their content is inlined earlier), schema
``.inc`` fragments are inlined, and the two system includes are hoisted and
de-duplicated at the top.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).resolve().parent / "plugin-api-modules.json"

PROJECT_INCLUDE = re.compile(r'^\s*#\s*include\s*"(neverc/Plugin/[^"]+)"')
SYSTEM_INCLUDE = re.compile(r"^\s*#\s*include\s*<([^>]+)>")

BANNER = [
    "/*===-- NevercPluginAPI.h - Aggregated NeverC plugin C ABI -------*- C -*-===*\\",
    "|*                                                                            *|",
    "|*  GENERATED FILE - DO NOT EDIT.                                             *|",
    "|*  Regenerate with: python3 utils/plugin-api/gen-single-header.py            *|",
    "|*                                                                            *|",
    "|*  Distributed single-header form of the first public NeverC plugin ABI.     *|",
    "|*  It inlines every public domain module in canonical order so a plugin can  *|",
    "|*  build against one self-contained pure-C header.                           *|",
    "\\*===----------------------------------------------------------------------===*/",
]


def load_manifest(path: Path) -> dict:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1:
        raise ValueError("unsupported plugin-api module manifest version")
    modules = document.get("modules")
    if not isinstance(modules, list) or not modules:
        raise ValueError("manifest must declare modules")
    seen: set[str] = set()
    for index, module in enumerate(modules):
        name = module.get("name")
        if not isinstance(name, str) or not name.endswith(".h"):
            raise ValueError(f"module {index} has an invalid name")
        if name in seen:
            raise ValueError(f"duplicate module: {name}")
        for dependency in module.get("depends_on", []):
            if dependency not in seen:
                raise ValueError(
                    f"module {name} depends on {dependency}, which is not "
                    "declared earlier in canonical order"
                )
        seen.add(name)
    return document


def strip_guard(name: str, guard: str, lines: list[str]) -> list[str]:
    ifndef = f"#ifndef {guard}"
    define = f"#define {guard}"
    start = next((i for i, line in enumerate(lines) if line.strip() == ifndef), None)
    if start is None or lines[start + 1].strip() != define:
        raise ValueError(f"{name} has no canonical include guard {guard}")
    end = next(
        (i for i in range(len(lines) - 1, -1, -1) if lines[i].strip().startswith("#endif")),
        None,
    )
    if end is None or end <= start + 1:
        raise ValueError(f"{name} has no closing include guard")
    # Keep the guard intact (from #ifndef through the closing #endif) and drop
    # the per-file license banner that precedes it plus any trailing content.
    return lines[start : end + 1]


def inline_module(
    include_root: Path, name: str, guard: str, digest: "hashlib._Hash"
) -> list[str]:
    path = include_root / name
    raw = path.read_text(encoding="utf-8")
    digest.update(name.encode("utf-8"))
    digest.update(raw.encode("utf-8"))
    lines = raw.splitlines()
    body = strip_guard(name, guard, lines)
    output: list[str] = []
    for line in body:
        system = SYSTEM_INCLUDE.match(line)
        if system:
            # Hoisted and de-duplicated at the top of the single header.
            continue
        project = PROJECT_INCLUDE.match(line)
        if project:
            target = project.group(1)
            if target.startswith("neverc/Plugin/Schema/") and target.endswith(".inc"):
                inc_path = ROOT / "neverc/include" / target
                inc_raw = inc_path.read_text(encoding="utf-8")
                digest.update(target.encode("utf-8"))
                digest.update(inc_raw.encode("utf-8"))
                output.append(f"/* inlined: {target} */")
                output.extend(inc_raw.splitlines())
                continue
            # Ordinary project module include: content is inlined earlier.
            output.append(f"/* inlined earlier: {target} */")
            continue
        output.append(line)
    return output


def generate(document: dict) -> str:
    include_root = ROOT / document["include_root"]
    system_includes = document.get("system_includes", [])
    modules = [m for m in document["modules"] if m.get("single_header")]

    digest = hashlib.sha256()
    bodies: list[str] = []
    for module in modules:
        bodies.append(f"/* ===== module: {module['name']} ===== */")
        bodies.extend(inline_module(include_root, module["name"], module["guard"], digest))
        bodies.append("")

    lines: list[str] = list(BANNER)
    lines.append("")
    lines.append(f"/* generated-from-digest: {digest.hexdigest()} */")
    lines.append("")
    lines.append("#ifndef NEVERC_PLUGIN_NEVERCPLUGINAPI_H")
    lines.append("#define NEVERC_PLUGIN_NEVERCPLUGINAPI_H")
    lines.append("")
    for header in system_includes:
        lines.append(f"#include <{header}>")
    lines.append("")
    lines.extend(bodies)
    lines.append("#endif /* NEVERC_PLUGIN_NEVERCPLUGINAPI_H */")
    text = "\n".join(lines).rstrip("\n") + "\n"
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the committed single header is out of date",
    )
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--output", type=Path, default=None)
    arguments = parser.parse_args()

    try:
        document = load_manifest(arguments.manifest)
        output_path = arguments.output or (ROOT / document["single_header"])
        generated = generate(document)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"single-header generation error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        try:
            existing = output_path.read_text(encoding="utf-8")
        except OSError:
            existing = ""
        if existing != generated:
            print(
                f"{output_path} is out of date; run gen-single-header.py",
                file=sys.stderr,
            )
            return 1
        return 0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

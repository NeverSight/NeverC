#!/usr/bin/env python3

"""Validate the host's compiled-in plugin phase inventory.

The generated ``Schema/*.inc`` fragments are compiled verbatim into the host, so
verifying they are current against their JSON sources validates the runtime
inventory without loading a plugin. This also confirms the coverage manifest
references only real schema phases (no ghosts) and reports how many stable phases
are covered so the final release gate can require completeness.

When ``--compiler`` is given, this additionally runs
``<compiler> --print-plugin-capabilities=json`` and cross-checks the *actual
binary* against ``PhaseSchema.json`` field by field. That catches the case the
file-level checks cannot: a schema that was regenerated but whose ``.inc`` was
never recompiled into the shipped host, so the source and the binary silently
disagree.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/PhaseSchema.json"
COVERAGE = ROOT / "docs/plugin-api/coverage.json"

SCHEMA_GENERATORS = [
    "gen-phase-schema.py",
    "gen-prep-schema.py",
    "gen-ast-schema.py",
    "gen-ir-schema.py",
    "gen-mir-schema.py",
    "gen-mc-schema.py",
    "gen-object-schema.py",
]


def run_generator_check(script: str) -> int:
    result = subprocess.run(
        [sys.executable, str(HERE / script), "--check"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
    return result.returncode


def load_runtime_inventory(compiler: str) -> dict:
    """Run the host's read-only capability query and return the parsed JSON."""
    result = subprocess.run(
        [compiler, "--print-plugin-capabilities=json"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise ValueError(
            "compiler capability query failed "
            f"(exit {result.returncode}):\n{result.stderr}"
        )
    return json.loads(result.stdout)


def _norm_id(value) -> tuple:
    # Schema and runtime both encode IDs as a 2-element list of hex strings;
    # normalize to integers so formatting differences never matter.
    return tuple(int(part, 16) for part in value)


def check_runtime_inventory(schema: dict, inventory: dict) -> int:
    status = 0

    expected_count = len(schema["phases"])
    runtime_count = inventory.get("phase_count")
    if runtime_count != expected_count:
        print(
            "check-phase-inventory: binary reports "
            f"{runtime_count} phases but the schema has {expected_count}; "
            "the host is stale (rebuild required)",
            file=sys.stderr,
        )
        status = 1

    schema_by_name = {p["name"]: p for p in schema["phases"]}
    runtime_by_name = {p["name"]: p for p in inventory.get("phases", [])}

    missing = sorted(set(schema_by_name) - set(runtime_by_name))
    extra = sorted(set(runtime_by_name) - set(schema_by_name))
    for name in missing:
        print(f"check-phase-inventory: binary is missing phase {name}",
              file=sys.stderr)
        status = 1
    for name in extra:
        print(f"check-phase-inventory: binary has ghost phase {name}",
              file=sys.stderr)
        status = 1

    for name in sorted(set(schema_by_name) & set(runtime_by_name)):
        want = schema_by_name[name]
        have = runtime_by_name[name]
        for field in ("domain", "kind", "verifier", "gate", "stability"):
            if want.get(field) != have.get(field):
                print(
                    f"check-phase-inventory: {name} {field} drift: "
                    f"schema={want.get(field)!r} binary={have.get(field)!r}",
                    file=sys.stderr,
                )
                status = 1
        for field in ("id", "input_artifact", "output_artifact"):
            if _norm_id(want[field]) != _norm_id(have[field]):
                print(
                    f"check-phase-inventory: {name} {field} drift between "
                    "schema and binary",
                    file=sys.stderr,
                )
                status = 1
        for field in ("policy", "observer_points"):
            if set(want.get(field, [])) != set(have.get(field, [])):
                print(
                    f"check-phase-inventory: {name} {field} drift: "
                    f"schema={sorted(want.get(field, []))} "
                    f"binary={sorted(have.get(field, []))}",
                    file=sys.stderr,
                )
                status = 1
        if bool(want.get("builtin_fallback")) != bool(have.get("builtin_fallback")):
            print(
                f"check-phase-inventory: {name} builtin_fallback drift: "
                f"schema={want.get('builtin_fallback')} "
                f"binary={have.get('builtin_fallback')}",
                file=sys.stderr,
            )
            status = 1

    if status == 0:
        print(
            "check-phase-inventory: binary inventory matches the schema "
            f"({runtime_count} phases)"
        )
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=None,
                        help="host compiler; when given, cross-check the "
                             "binary's runtime inventory against the schema")
    parser.add_argument("--require-full-coverage", action="store_true",
                        help="fail if any stable phase is uncovered")
    arguments = parser.parse_args()

    status = 0
    for script in SCHEMA_GENERATORS:
        status |= run_generator_check(script)
    if status:
        print("check-phase-inventory: compiled-in schema fragments are stale",
              file=sys.stderr)
        return 1

    try:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        coverage = json.loads(COVERAGE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"check-phase-inventory: {error}", file=sys.stderr)
        return 1

    if arguments.compiler:
        try:
            inventory = load_runtime_inventory(arguments.compiler)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print(f"check-phase-inventory: {error}", file=sys.stderr)
            return 1
        if check_runtime_inventory(schema, inventory):
            return 1

    schema_phases = {p["name"] for p in schema["phases"]}
    coverage_phases = {p["phase"] for p in coverage["phases"]}

    ghosts = sorted(coverage_phases - schema_phases)
    if ghosts:
        print("check-phase-inventory: coverage references unknown phases:",
              file=sys.stderr)
        for name in ghosts:
            print(f"  {name}", file=sys.stderr)
        return 1

    uncovered = sorted(schema_phases - coverage_phases)
    print(
        f"check-phase-inventory: {len(coverage_phases)}/{len(schema_phases)} "
        "stable phases covered"
    )
    if uncovered:
        print("check-phase-inventory: uncovered stable phases:")
        for name in uncovered:
            print(f"  {name}")
        if arguments.require_full_coverage:
            print("check-phase-inventory: full coverage required but missing",
                  file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

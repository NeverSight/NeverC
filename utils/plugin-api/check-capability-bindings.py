#!/usr/bin/env python3

"""Cross-check the plugin capability bindings across every source of truth.

The public capability set is described in several places that must agree: the
phase schema (compiled-in policies), the coverage manifest (tested policies), the
SDK manifest (interface versions and schema digests), and the ABI manifest. This
rejects policy drift between the schema and coverage, ghost coverage entries, and
stale SDK/ABI manifests, so a capability cannot be advertised in one place and
silently differ in another.

When ``--compiler`` is given it also runs
``<compiler> --print-plugin-capabilities=json`` and confirms the *running
binary* advertises exactly the ABI version and per-route policy the schema
promises. This is the binding that matters: a route cannot claim a policy bit
(INTERCEPTABLE/REPLACEABLE/...) in the schema while the shipped host exposes a
different policy, and a sealed gate cannot silently gain a replaceable bit.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SCHEMA = ROOT / "neverc/include/neverc/Plugin/Schema/PhaseSchema.json"
COVERAGE = ROOT / "docs/plugin-api/coverage.json"
SDK_MANIFEST = ROOT / "pluginsdk/manifest/plugin.json"
SCHEMA_DIR = ROOT / "neverc/include/neverc/Plugin/Schema"


def run_check(script: str, *args: str) -> int:
    result = subprocess.run(
        [sys.executable, str(HERE / script), "--check", *args],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
    return result.returncode


def check_policy_drift(schema: dict, coverage: dict) -> int:
    schema_policy = {p["name"]: list(p["policy"]) for p in schema["phases"]}
    status = 0
    for entry in coverage["phases"]:
        name = entry["phase"]
        if name not in schema_policy:
            print(f"check-capability-bindings: ghost coverage phase {name}",
                  file=sys.stderr)
            status = 1
            continue
        covered = list(entry.get("policies", []))
        if covered != schema_policy[name]:
            print(
                f"check-capability-bindings: policy drift for {name}:\n"
                f"  schema:   {schema_policy[name]}\n"
                f"  coverage: {covered}",
                file=sys.stderr,
            )
            status = 1
    return status


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


def check_runtime_bindings(schema: dict, manifest: dict, inventory: dict) -> int:
    status = 0

    abi = inventory.get("abi", {})
    want_abi = manifest.get("abi", {})
    if (abi.get("major"), abi.get("minor")) != (
        want_abi.get("major"),
        want_abi.get("minor"),
    ):
        print(
            "check-capability-bindings: binary ABI "
            f"{abi.get('major')}.{abi.get('minor')} does not match the SDK "
            f"manifest {want_abi.get('major')}.{want_abi.get('minor')}",
            file=sys.stderr,
        )
        status = 1

    schema_policy = {p["name"]: set(p["policy"]) for p in schema["phases"]}
    runtime_policy = {
        p["name"]: set(p.get("policy", [])) for p in inventory.get("phases", [])
    }
    if set(schema_policy) != set(runtime_policy):
        print(
            "check-capability-bindings: binary phase set differs from the "
            "schema; the host is stale (rebuild required)",
            file=sys.stderr,
        )
        status = 1
    for name in sorted(set(schema_policy) & set(runtime_policy)):
        if schema_policy[name] != runtime_policy[name]:
            print(
                f"check-capability-bindings: {name} policy binding drift:\n"
                f"  schema: {sorted(schema_policy[name])}\n"
                f"  binary: {sorted(runtime_policy[name])}",
                file=sys.stderr,
            )
            status = 1

    if status == 0:
        print("check-capability-bindings: binary policy bindings match the "
              "schema")
    return status


def check_schema_digests(manifest: dict) -> int:
    status = 0
    recorded = {s["name"]: s["digest"] for s in manifest.get("schemas", [])}
    for path in sorted(SCHEMA_DIR.glob("*.json")):
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if recorded.get(path.name) != actual:
            print(
                f"check-capability-bindings: SDK manifest schema digest for "
                f"{path.name} is stale",
                file=sys.stderr,
            )
            status = 1
    return status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=None,
                        help="host compiler; when given, cross-check the "
                             "binary's advertised ABI and policy bindings")
    arguments = parser.parse_args()

    status = 0
    # The SDK and ABI manifests must be current before their contents are trusted.
    status |= run_check("gen-sdk-manifest.py")
    status |= run_check("gen-abi-manifest.py")

    try:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        coverage = json.loads(COVERAGE.read_text(encoding="utf-8"))
        manifest = json.loads(SDK_MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"check-capability-bindings: {error}", file=sys.stderr)
        return 1

    status |= check_policy_drift(schema, coverage)
    status |= check_schema_digests(manifest)

    if arguments.compiler:
        try:
            inventory = load_runtime_inventory(arguments.compiler)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print(f"check-capability-bindings: {error}", file=sys.stderr)
            return 1
        status |= check_runtime_bindings(schema, manifest, inventory)

    if status == 0:
        print("check-capability-bindings: schema, coverage and manifests agree")
    return 1 if status else 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path


class CoverageError(ValueError):
    pass


COMMON_PHASE_FIELDS = {
    "phase",
    "input_artifact",
    "output_artifact",
    "policies",
    "observer",
    "provider_available",
    "verification",
}
CONDITIONAL_PHASE_FIELDS = {
    "interceptor_test",
    "default_executor_test",
    "replacement_test",
    "skip_proof_tests",
    "sealed_gate",
}


def require(condition, message):
    if not condition:
        raise CoverageError(message)


def require_object(value, name, fields):
    require(isinstance(value, dict), f"{name} must be an object")
    missing = fields - value.keys()
    if missing:
        raise CoverageError(f"{name} is missing {sorted(missing)[0]}")
    extra = value.keys() - fields
    if extra:
        raise CoverageError(
            f"{name} contains unknown field {sorted(extra)[0]}"
        )


def collect_test_references(value, path="manifest"):
    references = []
    if isinstance(value, dict):
        for key, item in value.items():
            child_path = f"{path}.{key}"
            if key.endswith("_test"):
                require(
                    isinstance(item, str) and item,
                    f"{child_path} must name a CTest test",
                )
                references.append((child_path, item))
            else:
                references.extend(collect_test_references(item, child_path))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            references.extend(
                collect_test_references(item, f"{path}[{index}]")
            )
    return references


def validate_observer(entry, policy, phase_name):
    observer = entry["observer"]
    observable = "OBSERVABLE" in policy
    if observable:
        require_object(
            observer,
            f"phase '{phase_name}' observer",
            {"available", "read_only_test"},
        )
        require(
            observer["available"] is True,
            f"phase '{phase_name}' observer must be available",
        )
    else:
        require_object(
            observer,
            f"phase '{phase_name}' observer",
            {"available"},
        )
        require(
            observer["available"] is False,
            f"phase '{phase_name}' observer must be unavailable",
        )


def validate_verification(entry, schema_phase, sealed, phase_name):
    verification = entry["verification"]
    require_object(
        verification,
        f"phase '{phase_name}' verification",
        {"kind", "id", "positive_test", "negative_test"},
    )
    expected_kind = "gate" if sealed else "verifier"
    expected_id = (
        schema_phase.get("gate")
        if sealed
        else schema_phase.get("verifier")
    )
    require(
        verification["kind"] == expected_kind,
        f"phase '{phase_name}' verification kind must be {expected_kind}",
    )
    require(
        isinstance(expected_id, str)
        and expected_id
        and verification["id"] == expected_id,
        f"phase '{phase_name}' verification id disagrees with schema",
    )


def validate_sealed_gate(entry, phase_name):
    sealed_gate = entry["sealed_gate"]
    fields = {
        "host_private",
        "provider_available",
        "interceptor_available",
        "skip_available",
        "host_executor_test",
        "bypass_negative_test",
    }
    require_object(
        sealed_gate, f"phase '{phase_name}' sealed_gate", fields
    )
    require(
        sealed_gate["host_private"] is True,
        f"phase '{phase_name}' sealed gate must be host-private",
    )
    for capability in (
        "provider_available",
        "interceptor_available",
        "skip_available",
    ):
        require(
            sealed_gate[capability] is False,
            f"phase '{phase_name}' sealed gate must not expose "
            f"{capability.removesuffix('_available')}",
        )


def validate_phase_entry(entry, schema_phase):
    phase_name = schema_phase["name"]
    require(isinstance(entry, dict), f"phase '{phase_name}' must be an object")
    missing = COMMON_PHASE_FIELDS - entry.keys()
    if missing:
        raise CoverageError(
            f"phase '{phase_name}' is missing {sorted(missing)[0]}"
        )
    extra = entry.keys() - COMMON_PHASE_FIELDS - CONDITIONAL_PHASE_FIELDS
    if extra:
        raise CoverageError(
            f"phase '{phase_name}' contains unknown field "
            f"{sorted(extra)[0]}"
        )

    require(
        entry["input_artifact"] == schema_phase["input_artifact"],
        f"phase '{phase_name}' input_artifact disagrees with schema",
    )
    require(
        entry["output_artifact"] == schema_phase["output_artifact"],
        f"phase '{phase_name}' output_artifact disagrees with schema",
    )
    require(
        entry["policies"] == schema_phase["policy"],
        f"phase '{phase_name}' policies disagree with schema",
    )

    policy = set(schema_phase["policy"])
    sealed = "SEALED_HOST_GATE" in policy
    interceptable = "INTERCEPTABLE" in policy and not sealed
    replaceable = "REPLACEABLE" in policy and not sealed
    skippable = "SKIPPABLE_WITH_PROOF" in policy and not sealed

    validate_observer(entry, policy, phase_name)
    require(
        isinstance(entry["provider_available"], bool),
        f"phase '{phase_name}' provider_available must be boolean",
    )
    require(
        entry["provider_available"] is replaceable,
        f"phase '{phase_name}' provider availability disagrees with policy",
    )
    validate_verification(entry, schema_phase, sealed, phase_name)

    if interceptable:
        require(
            "interceptor_test" in entry,
            f"phase '{phase_name}' is missing interceptor_test",
        )
    else:
        require(
            "interceptor_test" not in entry,
            f"phase '{phase_name}' must not contain interceptor_test",
        )

    if sealed:
        for field in (
            "default_executor_test",
            "replacement_test",
            "skip_proof_tests",
        ):
            require(
                field not in entry,
                f"phase '{phase_name}' must not contain {field}",
            )
        require(
            "sealed_gate" in entry,
            f"phase '{phase_name}' is missing sealed_gate",
        )
        validate_sealed_gate(entry, phase_name)
    else:
        require(
            "sealed_gate" not in entry,
            f"phase '{phase_name}' must not contain sealed_gate",
        )
        require(
            "default_executor_test" in entry,
            f"phase '{phase_name}' is missing default_executor_test",
        )

        if replaceable:
            require(
                "replacement_test" in entry,
                f"phase '{phase_name}' is missing replacement_test",
            )
        else:
            require(
                "replacement_test" not in entry,
                f"phase '{phase_name}' must not contain replacement_test",
            )

        if skippable:
            require(
                "skip_proof_tests" in entry,
                f"phase '{phase_name}' is missing skip_proof_tests",
            )
            require_object(
                entry["skip_proof_tests"],
                f"phase '{phase_name}' skip_proof_tests",
                {"valid_test", "stale_test", "cross_session_test"},
            )
        else:
            require(
                "skip_proof_tests" not in entry,
                f"phase '{phase_name}' must not contain skip_proof_tests",
            )


def validate_coverage(schema, manifest, test_names):
    require(isinstance(schema, dict), "phase schema must be an object")
    require(
        isinstance(schema.get("phases"), list),
        "phase schema must contain phases",
    )
    require(isinstance(manifest, dict), "coverage manifest must be an object")
    required_manifest_fields = {"schema_version", "phase_schema", "phases"}
    missing_manifest_fields = required_manifest_fields - manifest.keys()
    require(
        not missing_manifest_fields,
        "coverage manifest is missing "
        f"{sorted(missing_manifest_fields)[0]}"
        if missing_manifest_fields
        else "",
    )
    extra_manifest_fields = (
        manifest.keys()
        - required_manifest_fields
        - {"semantic_replay"}
    )
    require(
        not extra_manifest_fields,
        "coverage manifest contains unknown field "
        f"{sorted(extra_manifest_fields)[0]}"
        if extra_manifest_fields
        else "",
    )
    require(
        manifest["schema_version"] == 1,
        "coverage manifest schema_version must be 1",
    )
    require(
        isinstance(manifest["phase_schema"], str)
        and manifest["phase_schema"],
        "coverage manifest phase_schema must be a path",
    )
    require(
        isinstance(manifest["phases"], list),
        "coverage manifest phases must be an array",
    )
    if "semantic_replay" in manifest:
        replay = manifest["semantic_replay"]
        require_object(
            replay,
            "semantic_replay",
            {
                "ast_product",
                "semantic_product",
                "supported_concrete_kinds",
                "unsupported_kind_policy",
                "plugin_sema_provider_test",
                "unmatched_custom_product_test",
            },
        )
        for product_name in ("ast_product", "semantic_product"):
            require(
                isinstance(replay[product_name], list)
                and len(replay[product_name]) == 2
                and all(
                    isinstance(component, str) and component
                    for component in replay[product_name]
                ),
                f"semantic_replay.{product_name} must be a two-part ID",
            )
        kinds = replay["supported_concrete_kinds"]
        require(
            isinstance(kinds, list) and kinds,
            "semantic_replay must list supported concrete kinds",
        )
        seen_kinds = set()
        for index, kind in enumerate(kinds):
            require_object(
                kind,
                f"semantic_replay.supported_concrete_kinds[{index}]",
                {"kind", "positive_test"},
            )
            require(
                isinstance(kind["kind"], str)
                and kind["kind"]
                and kind["kind"] not in seen_kinds,
                "semantic_replay concrete kinds must be named and unique",
            )
            seen_kinds.add(kind["kind"])
        require_object(
            replay["unsupported_kind_policy"],
            "semantic_replay.unsupported_kind_policy",
            {"status", "negative_test", "representative_kind"},
        )

    stable_phases = [
        phase for phase in schema["phases"]
        if phase.get("stability") == "stable"
    ]
    schema_by_name = {phase["name"]: phase for phase in stable_phases}
    manifest_by_name = {}
    for entry in manifest["phases"]:
        require(
            isinstance(entry, dict) and isinstance(entry.get("phase"), str),
            "coverage phase is missing phase",
        )
        phase_name = entry["phase"]
        require(
            phase_name in schema_by_name,
            f"coverage manifest references unknown phase '{phase_name}'",
        )
        require(
            phase_name not in manifest_by_name,
            f"coverage manifest duplicates phase '{phase_name}'",
        )
        manifest_by_name[phase_name] = entry

    missing = schema_by_name.keys() - manifest_by_name.keys()
    if missing:
        raise CoverageError(
            "coverage manifest is missing stable phase "
            f"'{sorted(missing)[0]}'"
        )
    require(
        len(manifest_by_name) == len(schema_by_name),
        "coverage manifest phase count disagrees with schema",
    )

    for schema_phase in stable_phases:
        validate_phase_entry(
            manifest_by_name[schema_phase["name"]], schema_phase
        )

    known_tests = set(test_names)
    for path, test_name in collect_test_references(manifest):
        require(
            test_name in known_tests,
            f"{path} references unknown CTest test '{test_name}'",
        )


def load_json(path, description):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CoverageError(f"cannot read {description} '{path}': {error}")


def ctest_inventory(binary_dir, ctest_command):
    try:
        completed = subprocess.run(
            [
                ctest_command,
                "--test-dir",
                str(binary_dir),
                "--show-only=json-v1",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        raise CoverageError(f"cannot execute CTest: {error}")
    require(
        completed.returncode == 0,
        "CTest inventory failed: "
        + (completed.stderr.strip() or completed.stdout.strip()),
    )
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise CoverageError(f"CTest inventory is not valid JSON: {error}")
    tests = document.get("tests")
    require(isinstance(tests, list), "CTest inventory has no tests array")
    names = set()
    for test in tests:
        require(
            isinstance(test, dict)
            and isinstance(test.get("name"), str)
            and test["name"],
            "CTest inventory contains an invalid test",
        )
        names.add(test["name"])
    return names


def inventory_file(path):
    document = load_json(path, "CTest inventory")
    tests = document.get("tests")
    require(isinstance(tests, list), "CTest inventory has no tests array")
    return {
        test["name"]
        for test in tests
        if isinstance(test, dict) and isinstance(test.get("name"), str)
    }


def parse_arguments():
    repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="Validate NeverC plugin phase coverage"
    )
    parser.add_argument("manifest_path", type=Path, nargs="?")
    parser.add_argument(
        "--schema",
        type=Path,
        default=repository
        / "neverc/include/neverc/Plugin/Schema/PhaseSchema.json",
    )
    parser.add_argument("--manifest", type=Path)
    inventory = parser.add_mutually_exclusive_group()
    inventory.add_argument("--ctest-binary-dir", type=Path)
    inventory.add_argument("--ctest-inventory", type=Path)
    parser.add_argument("--ctest", default="ctest")
    arguments = parser.parse_args()
    if arguments.manifest is not None and arguments.manifest_path is not None:
        parser.error("manifest may be passed either positionally or with --manifest")
    arguments.manifest = (
        arguments.manifest
        or arguments.manifest_path
        or repository / "docs/plugin-api/coverage.json"
    )
    if (
        arguments.ctest_binary_dir is None
        and arguments.ctest_inventory is None
    ):
        arguments.ctest_binary_dir = (
            repository / "build-neverc/neverc/tests"
        )
    return arguments


def main():
    arguments = parse_arguments()
    schema = load_json(arguments.schema, "phase schema")
    manifest = load_json(arguments.manifest, "coverage manifest")
    if arguments.ctest_binary_dir is not None:
        tests = ctest_inventory(arguments.ctest_binary_dir, arguments.ctest)
    else:
        tests = inventory_file(arguments.ctest_inventory)
    validate_coverage(schema, manifest, tests)
    print(
        f"validated {len(manifest['phases'])} stable plugin phases "
        f"against {len(tests)} CTest tests"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except CoverageError as error:
        print(f"coverage error: {error}", file=sys.stderr)
        sys.exit(1)

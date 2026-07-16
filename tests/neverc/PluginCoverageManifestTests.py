#!/usr/bin/env python3

import copy
import importlib.util
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = (
    ROOT
    / "neverc/include/neverc/Plugin/Schema/PhaseSchema.json"
)
MANIFEST = ROOT / "docs/plugin-api/coverage.json"
CHECKER = ROOT / "utils/plugin-api/check-coverage.py"

SPEC = importlib.util.spec_from_file_location(
    "neverc_plugin_coverage_checker", CHECKER
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def referenced_tests(value):
    result = set()
    if isinstance(value, dict):
        for key, item in value.items():
            if key.endswith("_test"):
                result.add(item)
            else:
                result.update(referenced_tests(item))
    elif isinstance(value, list):
        for item in value:
            result.update(referenced_tests(item))
    return result


class PluginCoverageManifestTests(unittest.TestCase):
    def setUp(self):
        self.schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        self.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.inventory = referenced_tests(self.manifest)

    def validate(self, schema=None, manifest=None, inventory=None):
        MODULE.validate_coverage(
            self.schema if schema is None else schema,
            self.manifest if manifest is None else manifest,
            self.inventory if inventory is None else inventory,
        )

    def test_accepts_committed_manifest(self):
        self.validate()

    def test_rejects_missing_common_field(self):
        manifest = copy.deepcopy(self.manifest)
        del manifest["phases"][0]["input_artifact"]
        with self.assertRaisesRegex(MODULE.CoverageError, "input_artifact"):
            self.validate(manifest=manifest)

    def test_rejects_unknown_phase_and_test(self):
        manifest = copy.deepcopy(self.manifest)
        manifest["phases"][0]["phase"] = "neverc.driver.unknown"
        with self.assertRaisesRegex(MODULE.CoverageError, "unknown phase"):
            self.validate(manifest=manifest)

        manifest = copy.deepcopy(self.manifest)
        manifest["phases"][0]["interceptor_test"] = "Missing.Test"
        with self.assertRaisesRegex(MODULE.CoverageError, "Missing.Test"):
            self.validate(manifest=manifest)

    def test_enforces_interceptor_and_replacement_policy_fields(self):
        manifest = copy.deepcopy(self.manifest)
        del manifest["phases"][0]["interceptor_test"]
        with self.assertRaisesRegex(MODULE.CoverageError, "interceptor_test"):
            self.validate(manifest=manifest)

        schema = copy.deepcopy(self.schema)
        manifest = copy.deepcopy(self.manifest)
        schema["phases"][0]["policy"].remove("INTERCEPTABLE")
        manifest["phases"][0]["policies"].remove("INTERCEPTABLE")
        with self.assertRaisesRegex(MODULE.CoverageError, "must not contain"):
            self.validate(schema=schema, manifest=manifest)

        manifest = copy.deepcopy(self.manifest)
        replaceable = next(
            item for item in manifest["phases"]
            if "REPLACEABLE" in item["policies"]
        )
        del replaceable["replacement_test"]
        with self.assertRaisesRegex(MODULE.CoverageError, "replacement_test"):
            self.validate(manifest=manifest)

    def test_enforces_skip_proof_fields(self):
        schema = copy.deepcopy(self.schema)
        manifest = copy.deepcopy(self.manifest)
        schema["phases"][0]["policy"].append("SKIPPABLE_WITH_PROOF")
        manifest["phases"][0]["policies"].append("SKIPPABLE_WITH_PROOF")
        with self.assertRaisesRegex(MODULE.CoverageError, "skip_proof_tests"):
            self.validate(schema=schema, manifest=manifest)

        manifest["phases"][0]["skip_proof_tests"] = {
            "valid_test": next(iter(self.inventory)),
            "stale_test": next(iter(self.inventory)),
            "cross_session_test": next(iter(self.inventory)),
        }
        self.validate(schema=schema, manifest=manifest)

    def test_enforces_sealed_gate_shape(self):
        schema = copy.deepcopy(self.schema)
        manifest = copy.deepcopy(self.manifest)
        schema_phase = schema["phases"][0]
        manifest_phase = manifest["phases"][0]
        schema_phase["policy"] = ["OBSERVABLE", "SEALED_HOST_GATE"]
        schema_phase["gate"] = "sealed_verifier"
        manifest_phase["policies"] = ["OBSERVABLE", "SEALED_HOST_GATE"]
        manifest_phase["provider_available"] = False
        manifest_phase["verification"]["kind"] = "gate"
        manifest_phase["verification"]["id"] = "sealed_verifier"
        del manifest_phase["interceptor_test"]
        del manifest_phase["default_executor_test"]
        with self.assertRaisesRegex(MODULE.CoverageError, "sealed_gate"):
            self.validate(schema=schema, manifest=manifest)

        manifest_phase["sealed_gate"] = {
            "host_private": True,
            "provider_available": False,
            "interceptor_available": False,
            "skip_available": False,
            "host_executor_test": next(iter(self.inventory)),
            "bypass_negative_test": next(iter(self.inventory)),
        }
        self.validate(schema=schema, manifest=manifest)

        manifest_phase["sealed_gate"]["provider_available"] = True
        with self.assertRaisesRegex(MODULE.CoverageError, "provider"):
            self.validate(schema=schema, manifest=manifest)


if __name__ == "__main__":
    unittest.main()

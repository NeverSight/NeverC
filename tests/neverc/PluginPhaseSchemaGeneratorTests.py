#!/usr/bin/env python3

import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = (
    ROOT
    / "neverc/include/neverc/Plugin/Schema/PhaseSchema.json"
)
GENERATOR = ROOT / "utils/plugin-api/gen-phase-schema.py"

SPEC = importlib.util.spec_from_file_location(
    "neverc_phase_schema_generator", GENERATOR
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PhaseSchemaGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.document = json.loads(SCHEMA.read_text(encoding="utf-8"))

    def validate(self, document):
        with tempfile.TemporaryDirectory() as directory:
            schema = Path(directory) / "PhaseSchema.json"
            schema.write_text(json.dumps(document), encoding="utf-8")
            return MODULE.load_and_validate(schema)

    def test_accepts_committed_schema(self):
        phases, families = self.validate(self.document)
        self.assertEqual(len(phases), 130)
        self.assertEqual(len(families), 8)
        self.assertEqual(
            sum(phase["stability"] == "stable" for phase in phases), 96
        )
        self.assertEqual(
            sum(phase["stability"] == "experimental" for phase in phases), 34
        )
        self.assertEqual(
            sum(phase["domain"] == "dyncode" for phase in phases), 34
        )
        self.assertEqual(
            sum(phase["domain"] == "ir" for phase in phases), 8
        )
        self.assertEqual(
            sum(phase["domain"] == "mir" for phase in phases), 10
        )
        self.assertEqual(
            sum(phase["domain"] == "codegen" for phase in phases), 4
        )
        self.assertEqual(
            sum(phase["domain"] == "mc" for phase in phases), 13
        )
        self.assertEqual(
            sum(phase["domain"] == "assembly" for phase in phases), 4
        )
        self.assertEqual(
            sum(phase["domain"] == "object" for phase in phases), 8
        )
        self.assertEqual(
            sum(phase["domain"] == "link" for phase in phases), 20
        )

    def test_rejects_duplicate_phase_id(self):
        document = copy.deepcopy(self.document)
        document["phases"][1]["id"] = document["phases"][0]["id"]
        with self.assertRaisesRegex(ValueError, "duplicate phase ID"):
            self.validate(document)

    def test_rejects_missing_transition_verifier(self):
        document = copy.deepcopy(self.document)
        del document["phases"][0]["verifier"]
        with self.assertRaisesRegex(ValueError, "verifier"):
            self.validate(document)

    def test_rejects_sealed_replaceable_policy(self):
        document = copy.deepcopy(self.document)
        phase = document["phases"][0]
        phase["policy"].append("SEALED_HOST_GATE")
        phase["gate"] = "sealed_verifier"
        with self.assertRaisesRegex(ValueError, "sealed"):
            self.validate(document)

    def test_rejects_unstable_phase_order(self):
        document = copy.deepcopy(self.document)
        document["phases"][0], document["phases"][1] = (
            document["phases"][1],
            document["phases"][0],
        )
        with self.assertRaisesRegex(ValueError, "order"):
            self.validate(document)

    def test_rejects_after_commit_on_transition(self):
        document = copy.deepcopy(self.document)
        document["phases"][0]["observer_points"].append("AFTER_COMMIT")
        with self.assertRaisesRegex(ValueError, "AFTER_COMMIT"):
            self.validate(document)

    def test_rejects_replaceable_event(self):
        document = copy.deepcopy(self.document)
        event = next(
            phase for phase in document["phases"]
            if phase["kind"] == "event"
        )
        event["policy"].extend(["INTERCEPTABLE", "REPLACEABLE"])
        event["builtin_fallback"] = True
        with self.assertRaisesRegex(ValueError, "event"):
            self.validate(document)

    def test_rejects_extension_range_overlapping_builtin(self):
        document = copy.deepcopy(self.document)
        phase_id = document["phases"][0]["id"]
        family = document["extension_families"][0]
        family["id_high"] = phase_id[0]
        family["id_low_min"] = phase_id[1]
        family["id_low_max"] = phase_id[1]
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.validate(document)


if __name__ == "__main__":
    unittest.main()

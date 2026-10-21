#!/usr/bin/env python3
"""Exercise the real diagnostic producer and failure modes of its policy."""
import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "utils/ci/check-python-plugin-artifacts.py"
SPEC = importlib.util.spec_from_file_location("artifact_policy", SCRIPT)
POLICY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POLICY)


class ArtifactProducerPolicyTest(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / ".github/workflows/diagnose-cpp-template-source.yml"
        self.text = self.path.read_text()

    def test_actual_diagnostic_archives_are_governed(self):
        self.assertTrue(POLICY.is_producer(self.text))
        self.assertEqual(POLICY.check_producer(self.path, self.text), [])
        self.assertEqual(POLICY.compiler_archives(self.text), {
            "windows-arm64-neverc.zip", "windows-arm64-neverc-compiler.zip"})

    def test_each_missing_relocation_check_is_rejected(self):
        for archive in sorted(POLICY.compiler_archives(self.text)):
            with self.subTest(archive=archive):
                changed = self.text.replace("--archive " + archive, "--archive omitted.zip")
                failures = POLICY.check_producer(self.path, changed)
                self.assertIn("archive is not relocation-tested: " + archive, failures)

    def test_wrong_python_patch_is_rejected(self):
        changed = self.text.replace('python-version: "3.12.10"', 'python-version: "3.12.11"')
        self.assertIn('missing python-version: "3.12.10"', POLICY.check_producer(self.path, changed))

    def test_all_original_and_diagnostic_producers_are_required_by_name(self):
        actual = {path.name for path in POLICY.WORKFLOWS.glob("*.yml")
                  if POLICY.is_producer(path.read_text())}
        self.assertEqual(actual, POLICY.EXPECTED_PRODUCERS)
        self.assertEqual(len(actual), 13)
        for name in actual:
            with self.subTest(producer=name):
                self.assertEqual(POLICY.EXPECTED_PRODUCERS - (actual - {name}), {name})
        swapped = actual - {"build-windows-arm64.yml"} | {"unexpected-compiler.yml"}
        self.assertEqual(POLICY.EXPECTED_PRODUCERS - swapped, {"build-windows-arm64.yml"})
        self.assertEqual(swapped - POLICY.EXPECTED_PRODUCERS, {"unexpected-compiler.yml"})

    def test_complete_repository_contract(self):
        result = subprocess.run([sys.executable, str(SCRIPT)], cwd=ROOT,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("13 producers and all compiler archives are governed", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)

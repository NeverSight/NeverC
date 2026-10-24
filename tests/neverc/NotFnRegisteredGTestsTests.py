"""Pure Python source-boundary regressions; no C++ compiler or program is invoked."""
from pathlib import Path
import hashlib
import importlib.util
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class RegisteredNotFnGTestsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adapter = module('registered_not_fn', 'utils/ci/not_fn_registered_gtests.py')
        cls.extractor = module('original_cpp_gtests', 'utils/ci/extract_cpp_gtests.py')
        cls.registered = (ROOT / 'tests/neverc/TranslateTests.cpp').read_bytes()
        cls.additions = (ROOT / 'utils/ci/not-fn-pointer-ten-formal-additions.cpp').read_bytes()
        cls.original = cls.adapter.restore_original_gtests(cls.registered, cls.additions)

    def test_restores_the_exact_original_7493_source(self):
        self.assertEqual(hashlib.sha256(self.original).hexdigest(), self.adapter.ORIGINAL_SOURCE_SHA256)
        self.assertEqual(self.original + self.additions, self.registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.original, self.additions), self.original)

    def test_original_extractor_and_124_selected_bodies_are_identical(self):
        manifest = json.loads((ROOT / 'utils/ci/cpp-pair-piecewise-formal-tests.json').read_text())
        selected = manifest['new'] + manifest['regression']
        self.assertEqual(len(selected), 124)
        expected, proof = self.extractor.extract(self.original, selected)
        restored = self.adapter.restore_original_gtests(self.registered, self.additions)
        actual, actual_proof = self.extractor.extract(restored, selected)
        self.assertEqual(actual, expected)
        self.assertEqual(actual_proof, proof)
        self.assertEqual(proof['original_test_names'], 7493)
        self.assertEqual(proof['selected_count'], 124)
        self.assertEqual((actual + self.additions).count(b'TEST_F('), 134)
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.registered, selected)

    def test_preserves_archived_four_test_replay(self):
        four = (ROOT / 'utils/ci/not-fn-pointer-four-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(four + b'\n'))
        registered_four = self.original + four
        self.assertEqual(hashlib.sha256(registered_four).hexdigest(),
                         self.adapter.FOUR_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(registered_four, four), self.original)

    def test_preserves_archived_five_test_replay(self):
        five = (ROOT / 'utils/ci/not-fn-pointer-five-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(five + b'\n'))
        registered_five = self.original + five
        self.assertEqual(hashlib.sha256(registered_five).hexdigest(),
                         self.adapter.FIVE_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(registered_five, five), self.original)

    def test_preserves_archived_six_test_replay(self):
        six = (ROOT / 'utils/ci/not-fn-pointer-six-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(six + b'\n'))
        registered_six = self.original + six
        self.assertEqual(hashlib.sha256(registered_six).hexdigest(),
                         self.adapter.SIX_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(registered_six, six), self.original)

    def test_rejects_mismatched_registered_suffix_versions(self):
        suffixes = [(ROOT / ('utils/ci/not-fn-pointer-' + version + '-formal-additions.cpp')).read_bytes()
                    for version in ['four', 'five', 'six', 'ten']]
        for source_suffix in suffixes:
            for supplied_suffix in suffixes:
                with self.subTest(source=hashlib.sha256(source_suffix).hexdigest(),
                                  supplied=hashlib.sha256(supplied_suffix).hexdigest()):
                    source = self.original + source_suffix
                    if source_suffix == supplied_suffix:
                        self.assertEqual(self.adapter.restore_original_gtests(source, supplied_suffix), self.original)
                    else:
                        with self.assertRaises(AssertionError):
                            self.adapter.restore_original_gtests(source, supplied_suffix)

    def test_rejects_changed_original_source(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(b' ' + self.original[1:] + self.additions, self.additions)

    def test_rejects_changed_golden_body(self):
        changed = self.additions.replace(b'TEST_F(', b'TEST_X(', 1)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.original + changed, self.additions)

    def test_rejects_truncated_registration(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.registered[:-1], self.additions)

    def test_rejects_duplicated_registration(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.registered + self.additions, self.additions)

    def test_rejects_mutated_reference_additions(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.registered, self.additions[:-1])


if __name__ == '__main__':
    unittest.main()

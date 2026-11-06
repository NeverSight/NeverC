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
        cls.previous48 = (ROOT / 'utils/ci/bind-forty-eight-formal-additions.cpp').read_bytes()
        cls.previous61 = (ROOT / 'utils/ci/bind-sixty-one-formal-additions.cpp').read_bytes()
        cls.previous71 = (ROOT / 'utils/ci/bind-seventy-one-formal-additions.cpp').read_bytes()
        cls.previous74 = (ROOT / 'utils/ci/bind-seventy-four-formal-additions.cpp').read_bytes()
        cls.additions = (ROOT / 'utils/ci/bind-seventy-seven-formal-additions.cpp').read_bytes()
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
        self.assertEqual((actual + self.additions).count(b'TEST_F('), 201)
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

    def test_preserves_archived_ten_test_replay(self):
        ten = (ROOT / 'utils/ci/not-fn-pointer-ten-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(ten + b'\n'))
        self.assertEqual(hashlib.sha256(self.original + ten).hexdigest(), self.adapter.TEN_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(self.original + ten, ten), self.original)

    def test_preserves_archived_twelve_test_replay(self):
        twelve = (ROOT / 'utils/ci/not-fn-pointer-twelve-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(twelve))
        registered_twelve = self.original + twelve
        self.assertEqual(hashlib.sha256(registered_twelve).hexdigest(),
                         self.adapter.TWELVE_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(registered_twelve, twelve), self.original)

    def test_preserves_archived_twenty_one_test_replay(self):
        twenty_one = (ROOT / 'utils/ci/bind-twenty-one-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(twenty_one))
        self.assertEqual(hashlib.sha256(self.original + twenty_one).hexdigest(),
                         self.adapter.BIND21_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(self.original + twenty_one, twenty_one), self.original)

    def test_bind_call_eleven_follow_the_exact_original_twenty_one_goldens(self):
        twenty_one = (ROOT / 'utils/ci/bind-twenty-one-formal-additions.cpp').read_bytes()
        eleven = (ROOT / 'utils/ci/bind-call-arguments-eleven-formal-additions.cpp').read_bytes()
        self.assertEqual(eleven.count(b'TEST_F('), 11)
        thirty_two = (ROOT / 'utils/ci/bind-thirty-two-formal-additions.cpp').read_bytes()
        self.assertEqual(twenty_one + eleven, thirty_two)
        self.assertTrue(self.additions.startswith(thirty_two))
        self.assertTrue(self.registered.startswith(self.original + thirty_two))

    def test_bind_nine_follow_the_exact_original_twelve_goldens(self):
        twelve = (ROOT / 'utils/ci/not-fn-pointer-twelve-formal-additions.cpp').read_bytes()
        bind_nine = (ROOT / 'utils/ci/bind-fully-bound-nine-formal-additions.cpp').read_bytes()
        self.assertEqual(bind_nine.count(b'TEST_F('), 9)
        twenty_one = (ROOT / 'utils/ci/bind-twenty-one-formal-additions.cpp').read_bytes()
        self.assertEqual(twelve + bind_nine, twenty_one)
        self.assertEqual(self.original + twelve + bind_nine, self.original + twenty_one)

    def test_preserves_archived_thirty_two_test_replay(self):
        thirty_two = (ROOT / 'utils/ci/bind-thirty-two-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(thirty_two))
        self.assertEqual(hashlib.sha256(self.original + thirty_two).hexdigest(),
                         self.adapter.BIND32_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(self.original + thirty_two, thirty_two), self.original)

    def test_zero_bound_eight_follow_the_exact_previous_thirty_two_goldens(self):
        thirty_two = (ROOT / 'utils/ci/bind-thirty-two-formal-additions.cpp').read_bytes()
        eight = (ROOT / 'utils/ci/bind-zero-bound-eight-formal-additions.cpp').read_bytes()
        self.assertEqual(eight.count(b'TEST_F('), 8)
        forty = (ROOT / 'utils/ci/bind-forty-formal-additions.cpp').read_bytes()
        self.assertEqual(thirty_two + eight, forty)
        self.assertEqual(self.original + self.additions, self.registered)

    def test_preserves_archived_forty_test_replay(self):
        forty = (ROOT / 'utils/ci/bind-forty-formal-additions.cpp').read_bytes()
        self.assertTrue(self.additions.startswith(forty))
        self.assertEqual(hashlib.sha256(self.original + forty).hexdigest(),
                         self.adapter.BIND40_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(self.original + forty, forty), self.original)

    def test_expanded_bound_eight_follow_the_exact_previous_forty_goldens(self):
        forty = (ROOT / 'utils/ci/bind-forty-formal-additions.cpp').read_bytes()
        eight = (ROOT / 'utils/ci/bind-expanded-bound-eight-formal-additions.cpp').read_bytes()
        self.assertEqual(eight.count(b'TEST_F('), 8)
        self.assertEqual(forty + eight, self.previous48)
        self.assertEqual(self.original + self.additions, self.registered)

    def test_preserves_archived_forty_eight_test_replay(self):
        self.assertTrue(self.additions.startswith(self.previous48))
        self.assertEqual(hashlib.sha256(self.original + self.previous48).hexdigest(),
                         self.adapter.BIND48_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous48, self.previous48), self.original)

    def test_placeholder_thirteen_extend_the_exact_previous_forty_eight(self):
        thirteen = (ROOT / 'utils/ci/bind-placeholder-thirteen-formal-additions.cpp').read_bytes()
        self.assertEqual(thirteen.count(b'TEST_F('), 13)
        self.assertEqual(self.previous48 + thirteen, self.previous61)
        self.assertEqual(hashlib.sha256(self.original + self.previous61).hexdigest(),
                         self.adapter.BIND61_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.original + self.additions, self.registered)

    def test_preserves_archived_sixty_one_and_appends_exact_nested_ten(self):
        ten = (ROOT / 'utils/ci/bind-nested-ten-formal-additions.cpp').read_bytes()
        self.assertEqual(self.previous61 + ten, self.previous71)
        self.assertEqual(ten.count(b'TEST_F('), 10)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous61, self.previous61), self.original)
        self.assertEqual(hashlib.sha256(self.original + self.previous71).hexdigest(),
                         self.adapter.BIND71_REGISTERED_SOURCE_SHA256)

    def test_preserves_seventy_one_and_appends_exact_owned_three(self):
        three = (ROOT / 'utils/ci/bind-owned-three-formal-additions.cpp').read_bytes()
        self.assertEqual(self.previous71 + three, self.previous74)
        self.assertEqual(three.count(b'TEST_F('), 3)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous71, self.previous71), self.original)
        self.assertEqual(hashlib.sha256(self.original + self.previous74).hexdigest(),
                         self.adapter.BIND74_REGISTERED_SOURCE_SHA256)

    def test_preserves_seventy_four_and_appends_exact_borrowed_three(self):
        three = (ROOT / 'utils/ci/bind-reference-three-formal-additions.cpp').read_bytes()
        self.assertEqual(self.previous74 + three, self.additions)
        self.assertEqual(three.count(b'TEST_F('), 3)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous74, self.previous74), self.original)
        self.assertEqual(hashlib.sha256(self.registered).hexdigest(),
                         self.adapter.BIND77_REGISTERED_SOURCE_SHA256)

    def test_rejects_mismatched_registered_suffix_versions(self):
        suffixes = [(ROOT / ('utils/ci/not-fn-pointer-' + version + '-formal-additions.cpp')).read_bytes()
                    for version in ['four', 'five', 'six', 'ten', 'eleven', 'twelve']] + [
                        (ROOT / 'utils/ci/bind-twenty-one-formal-additions.cpp').read_bytes(),
                        (ROOT / 'utils/ci/bind-thirty-two-formal-additions.cpp').read_bytes(),
                        (ROOT / 'utils/ci/bind-forty-formal-additions.cpp').read_bytes(),
                        self.previous48, self.previous61, self.previous71, self.previous74, self.additions]
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

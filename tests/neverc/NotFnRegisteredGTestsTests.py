"""Pure Python source-boundary regressions; no C++ compiler or program is invoked."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def restore_previous_maintenance(source, manifest, extractor):
    assert hashlib.sha256(source).hexdigest() == manifest['maintained_original_sha256']
    for owner in manifest['test_owners']:
        fixture, name = owner['owner'].split('.')
        pattern = rb'(?m)^TEST_F\(\s*' + fixture.encode() + rb'\s*,\s*' + name.encode() + rb'\s*\)\s*\{'
        match, = list(re.finditer(pattern, source))
        end = extractor.body_end(source, match.end()-1)
        body = source[match.start():end]
        assert hashlib.sha256(body).hexdigest() == owner['maintained_body_sha256']
        for row in owner['programs']:
            before, after = row['before'].encode(), row['after'].encode()
            assert hashlib.sha256(before).hexdigest() == row['before_sha256']
            assert hashlib.sha256(after).hexdigest() == row['after_sha256']
            assert body.count(after) == 1
            body = body.replace(after, before, 1)
        assert hashlib.sha256(body).hexdigest() == owner['archived_body_sha256']
        source = source[:match.start()] + body + source[end:]
    assert hashlib.sha256(source).hexdigest() == manifest['previous_maintained_original_sha256']
    return source


class RegisteredNotFnGTestsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adapter = module('registered_not_fn', 'utils/ci/not_fn_registered_gtests.py')
        cls.extractor = module('original_cpp_gtests', 'utils/ci/extract_cpp_gtests.py')
        cls.registered = (ROOT / 'tests/neverc/TranslateTests.cpp').read_bytes()
        cls.definition_registered = cls.registered
        cls.definition_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.definition_original = cls.adapter.restore_original_gtests(cls.definition_registered, cls.definition_suffix)
        cls.registered = cls.adapter.restore_definition_fixture_history(cls.definition_registered)
        cls.deque_registered = cls.registered
        cls.deque_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_original = cls.adapter.restore_original_gtests(cls.deque_registered, cls.deque_suffix)
        cls.deque_manifest = json.loads((ROOT / 'utils/ci/r19-deque-gtests.json').read_text())
        assert hashlib.sha256(cls.deque_registered).hexdigest() == cls.adapter.R19_DEQUE_REGISTERED_SHA256
        assert hashlib.sha256(cls.deque_original).hexdigest() == cls.adapter.R19_DEQUE_MAINTAINED_SHA256
        first = cls.deque_manifest['new_gtest_names'][0].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first) + rb'\) \{', cls.deque_original)
        cls.deque_additions = cls.deque_original[match.start():]
        assert cls.deque_additions.count(b'TEST_F(') == 10
        cls.registered = cls.deque_original[:match.start()] + cls.deque_suffix
        cls.deque_adapter_registered = cls.registered
        cls.deque_adapter_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_adapter_original = cls.adapter.restore_original_gtests(cls.deque_adapter_registered, cls.deque_adapter_suffix)
        cls.deque_adapter_manifest = json.loads((ROOT / 'utils/ci/r19-deque-adapter-gtests.json').read_text())
        assert hashlib.sha256(cls.deque_adapter_registered).hexdigest() == cls.adapter.R19_DEQUE_ADAPTER_REGISTERED_SHA256
        assert hashlib.sha256(cls.deque_adapter_original).hexdigest() == cls.adapter.R19_DEQUE_ADAPTER_MAINTAINED_SHA256
        first = cls.deque_adapter_manifest['new_gtest_names'][0].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first) + rb'\) \{', cls.deque_adapter_original)
        cls.deque_adapter_additions = cls.deque_adapter_original[match.start():]
        assert cls.deque_adapter_additions.count(b'TEST_F(') == 10
        cls.registered = cls.deque_adapter_original[:match.start()] + cls.deque_adapter_suffix
        cls.list_registered = cls.registered
        cls.list_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.list_original = cls.adapter.restore_original_gtests(cls.list_registered, cls.list_suffix)
        cls.list_manifest = json.loads((ROOT / 'utils/ci/r19-list-gtests.json').read_text())
        assert hashlib.sha256(cls.list_registered).hexdigest() == cls.adapter.R19_LIST_REGISTERED_SHA256
        assert hashlib.sha256(cls.list_original).hexdigest() == cls.adapter.R19_LIST_MAINTAINED_SHA256
        first = cls.list_manifest['new_gtest_names'][0].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first) + rb'\) \{', cls.list_original)
        cls.list_additions = cls.list_original[match.start():]
        assert cls.list_additions.count(b'TEST_F(') == 10
        cls.registered = cls.list_original[:match.start()] + cls.list_suffix
        cls.forward_list_registered = cls.registered
        cls.forward_list_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.forward_list_original = cls.adapter.restore_original_gtests(cls.forward_list_registered, cls.forward_list_suffix)
        cls.forward_list_manifest = json.loads((ROOT / 'utils/ci/r19-forward-list-gtests.json').read_text())
        assert hashlib.sha256(cls.forward_list_registered).hexdigest() == cls.adapter.R19_FORWARD_LIST_REGISTERED_SHA256
        assert hashlib.sha256(cls.forward_list_original).hexdigest() == cls.adapter.R19_FORWARD_LIST_MAINTAINED_SHA256
        first = cls.forward_list_manifest['new_gtest_names'][0].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first) + rb'\) \{', cls.forward_list_original)
        cls.forward_list_additions = cls.forward_list_original[match.start():]
        assert cls.forward_list_additions.count(b'TEST_F(') == 10
        cls.registered = cls.forward_list_original[:match.start()] + cls.forward_list_suffix
        cls.valarray_registered = cls.registered
        cls.valarray_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.valarray_original = cls.adapter.restore_original_gtests(cls.valarray_registered, cls.valarray_suffix)
        cls.valarray_manifest = json.loads((ROOT / 'utils/ci/r19-valarray-gtests.json').read_text())
        assert hashlib.sha256(cls.valarray_registered).hexdigest() == cls.adapter.R19_VALARRAY_REGISTERED_SHA256
        assert hashlib.sha256(cls.valarray_original).hexdigest() == cls.adapter.R19_VALARRAY_MAINTAINED_SHA256
        first = cls.valarray_manifest['new_gtest_names'][0].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first) + rb'\) \{', cls.valarray_original)
        cls.valarray_additions = cls.valarray_original[match.start():]
        assert cls.valarray_additions.count(b'TEST_F(') == 10
        cls.registered = cls.valarray_original[:match.start()] + cls.valarray_suffix
        cls.r19_registered = cls.registered
        cls.r19_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.r19_original = cls.adapter.restore_original_gtests(cls.r19_registered, cls.r19_suffix)
        cls.r19_manifest = json.loads((ROOT / 'utils/ci/r19-scoped-bitset-gtests.json').read_text())
        assert hashlib.sha256(cls.r19_registered).hexdigest() == cls.adapter.R19_POPCOUNT_REGISTERED_SHA256
        assert hashlib.sha256(cls.r19_original).hexdigest() == cls.adapter.R19_POPCOUNT_MAINTAINED_SHA256
        first = cls.r19_manifest['new_gtest_names'][0].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first) + rb'\) \{', cls.r19_original)
        cls.r19_additions = cls.r19_original[match.start():]
        assert cls.r19_additions.count(b'TEST_F(') == 6
        cls.registered = cls.r19_original[:match.start()] + cls.r19_suffix
        cls.previous48 = (ROOT / 'utils/ci/bind-forty-eight-formal-additions.cpp').read_bytes()
        cls.previous61 = (ROOT / 'utils/ci/bind-sixty-one-formal-additions.cpp').read_bytes()
        cls.previous71 = (ROOT / 'utils/ci/bind-seventy-one-formal-additions.cpp').read_bytes()
        cls.previous74 = (ROOT / 'utils/ci/bind-seventy-four-formal-additions.cpp').read_bytes()
        cls.previous77 = (ROOT / 'utils/ci/bind-seventy-seven-formal-additions.cpp').read_bytes()
        cls.previous84 = (ROOT / 'utils/ci/bind-eighty-four-formal-additions.cpp').read_bytes()
        cls.additions = (ROOT / 'utils/ci/stl-ninety-seven-formal-additions.cpp').read_bytes()
        cls.latest_registered = cls.registered
        cls.latest_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.latest_twelve = (ROOT / 'utils/ci/bind-explicit-result-twelve-formal-additions.cpp').read_bytes()
        cls.latest_original = cls.adapter.restore_original_gtests(cls.latest_registered, cls.latest_suffix)
        cls.maintenance = json.loads((ROOT / 'utils/ci/stl-source-maintenance.json').read_text())
        cls.query_registered, cls.query_original = cls.latest_registered, cls.latest_original
        cls.query_additions = (ROOT / 'utils/ci/query-constructor-three-formal-additions.cpp').read_bytes()
        assert hashlib.sha256(cls.query_additions).hexdigest() == 'e9a94886790c6ad8987c21522a67f899953f167205f26155bb2d08b418553005'
        assert hashlib.sha256(cls.query_original).hexdigest() == cls.adapter.QUERY_CONSTRUCTOR_CHECKED_MAINTAINED_ORIGINAL_SHA256
        assert hashlib.sha256(cls.query_registered).hexdigest() == cls.adapter.QUERY_CONSTRUCTOR_CHECKED_REGISTERED_BIND109_SHA256
        assert cls.query_original.count(cls.query_additions) == 1
        assert cls.query_registered.count(cls.query_additions) == 1
        cls.latest_original = cls.query_original.replace(cls.query_additions, b'', 1)
        cls.latest_registered = cls.query_registered.replace(cls.query_additions, b'', 1)
        assert hashlib.sha256(cls.latest_original).hexdigest() == cls.maintenance['maintained_original_sha256']
        assert hashlib.sha256(cls.latest_registered).hexdigest() == cls.adapter.BIND109_REGISTERED_SOURCE_SHA256
        cls.previous_maintained_original = restore_previous_maintenance(
            cls.latest_original, cls.maintenance, cls.extractor)
        cls.registered = cls.previous_maintained_original + cls.additions
        cls.original = cls.adapter.restore_original_gtests(cls.registered, cls.additions)
        cls.current_registered, cls.current_original = cls.registered, cls.original
        fixed = b'extern "C" void* user_word_copy(void* destination, const void*, __SIZE_TYPE__ size) { auto* bytes=static_cast<unsigned char*>(destination); for(__SIZE_TYPE__ i=0;i<size;++i) bytes[i]=0; return destination; }'
        archived = b'extern "C" void* user_word_copy(void* destination, const void*, unsigned long size) { auto* bytes=static_cast<unsigned char*>(destination); for(unsigned long i=0;i<size;++i) bytes[i]=0; return destination; }'
        cls.target_size_helper_current, cls.target_size_helper_archived = fixed, archived
        assert cls.original.count(fixed) == 12
        cls.original = cls.original.replace(fixed, archived)
        assert hashlib.sha256(cls.original).hexdigest() == cls.adapter.ORIGINAL_SOURCE_SHA256
        cls.registered = cls.original + cls.additions
        assert hashlib.sha256(cls.registered).hexdigest() == cls.adapter.BIND97_REGISTERED_SOURCE_SHA256

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
        self.assertEqual((actual + self.additions).count(b'TEST_F('), 221)
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
        self.assertEqual(self.previous74 + three, self.previous77)
        self.assertEqual(three.count(b'TEST_F('), 3)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous74, self.previous74), self.original)
        self.assertEqual(hashlib.sha256(self.original + self.previous77).hexdigest(),
                         self.adapter.BIND77_REGISTERED_SOURCE_SHA256)

    def test_preserves_seventy_seven_and_appends_exact_standard_seven(self):
        seven = (ROOT / 'utils/ci/bind-standard-seven-formal-additions.cpp').read_bytes()
        self.assertEqual(self.previous77 + seven, self.previous84)
        self.assertEqual(seven.count(b'TEST_F('), 7)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous77, self.previous77), self.original)
        self.assertEqual(hashlib.sha256(self.original + self.previous84).hexdigest(),
                         self.adapter.BIND84_REGISTERED_SOURCE_SHA256)

    def test_preserves_eighty_four_and_appends_exact_member_thirteen(self):
        member = (ROOT / 'utils/ci/bind-member-thirteen-formal-additions.cpp').read_bytes()
        self.assertEqual(self.previous84 + member, self.additions)
        self.assertEqual(member.count(b'TEST_F('), 13)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.original + self.previous84, self.previous84), self.original)
        self.assertEqual(hashlib.sha256(self.registered).hexdigest(),
                         self.adapter.BIND97_REGISTERED_SOURCE_SHA256)

    def test_rejects_mismatched_registered_suffix_versions(self):
        suffixes = [(ROOT / ('utils/ci/not-fn-pointer-' + version + '-formal-additions.cpp')).read_bytes()
                    for version in ['four', 'five', 'six', 'ten', 'eleven', 'twelve']] + [
                        (ROOT / 'utils/ci/bind-twenty-one-formal-additions.cpp').read_bytes(),
                        (ROOT / 'utils/ci/bind-thirty-two-formal-additions.cpp').read_bytes(),
                        (ROOT / 'utils/ci/bind-forty-formal-additions.cpp').read_bytes(),
                        self.previous48, self.previous61, self.previous71, self.previous74, self.previous77, self.previous84, self.additions]
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


    def test_current_target_size_maintenance_has_exact_source_and_suffix(self):
        self.assertEqual(hashlib.sha256(self.current_registered).hexdigest(),
                         self.adapter.WINDOWS64_MAINTAINED_BIND97_SHA256)
        self.assertEqual(hashlib.sha256(self.current_original).hexdigest(),
                         self.adapter.WINDOWS64_MAINTAINED_ORIGINAL_SHA256)
        self.assertEqual(self.current_original + self.additions, self.current_registered)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.current_registered, self.additions), self.current_original)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.current_original, self.additions), self.current_original)

    def test_current_maintenance_preserves_124_cases_and_exact_target_size_repair(self):
        manifest = json.loads((ROOT / 'utils/ci/cpp-pair-piecewise-formal-tests.json').read_text())
        selected = manifest['new'] + manifest['regression']
        import re
        pattern = re.compile(rb'(?m)^TEST_F\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{')
        def bodies(source):
            matches = list(pattern.finditer(source))
            self.assertEqual(len(matches), 7493)
            result = {}
            for match in matches:
                name = match[1].decode() + '.' + match[2].decode()
                if name in selected:
                    end = self.extractor.body_end(source, match.end() - 1)
                    result[name] = source[match.start():end]
            self.assertEqual(set(result), set(selected))
            self.assertEqual(len(result), 124)
            return result
        current, archived = bodies(self.current_original), bodies(self.original)
        changed = {name for name in current if current[name] != archived[name]}
        owner = 'TranslateTest.CoreV2StringHashAdaptersRejectSelectedSourcesAndConversions'
        self.assertEqual(changed, {owner})
        self.assertEqual(archived[owner].count(self.target_size_helper_archived), 12)
        self.assertEqual(current[owner].count(self.target_size_helper_current), 12)
        self.assertEqual(current[owner], archived[owner].replace(
            self.target_size_helper_archived, self.target_size_helper_current))

    def test_current_maintenance_rejects_changed_source_and_mismatched_suffixes(self):
        sources = [self.current_registered[:-1], self.current_registered + self.additions,
                   b' ' + self.current_registered[1:],
                   self.current_registered.replace(b'__SIZE_TYPE__ size', b'unsigned long size', 1)]
        for source in sources:
            with self.subTest(source=hashlib.sha256(source).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, self.additions)
        for suffix in [self.previous48, self.previous61, self.previous71,
                       self.previous74, self.previous77, self.previous84, self.additions[:-1]]:
            with self.subTest(suffix=hashlib.sha256(suffix).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(self.current_registered, suffix)


    def test_latest109_preserves_exact97_and_twelve_executed_bodies(self):
        self.assertEqual(self.additions + self.latest_twelve, self.latest_suffix)
        self.assertEqual(self.latest_twelve.count(b'TEST_F('), 12)
        self.assertEqual(self.latest_suffix.count(b'TEST_F('), 109)
        self.assertEqual(self.latest_original + self.latest_suffix, self.latest_registered)
        self.assertEqual(hashlib.sha256(self.latest_registered).hexdigest(),
                         self.adapter.BIND109_REGISTERED_SOURCE_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.latest_registered, self.latest_suffix), self.latest_original)

    def test_latest_maintenance_is_exactly_eighteen_programs_in_five_owners(self):
        self.assertEqual(self.maintenance['programs'], 18)
        self.assertEqual(len(self.maintenance['test_owners']), 5)
        self.assertEqual(sum(len(row['programs']) for row in self.maintenance['test_owners']), 18)
        self.assertEqual(hashlib.sha256(self.previous_maintained_original).hexdigest(),
                         self.adapter.WINDOWS64_MAINTAINED_ORIGINAL_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.latest_original, self.latest_suffix), self.latest_original)
        latest97 = self.latest_original + self.additions
        self.assertEqual(hashlib.sha256(latest97).hexdigest(), self.adapter.FIXTURE_MAINTAINED_BIND97_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(latest97, self.additions), self.latest_original)

    def test_latest_twelve_source_and_body_hashes_match_the_actual_execution_contract(self):
        mapping = json.loads((ROOT / 'utils/ci/bind-explicit-result-formal-tests.json').read_text())
        cases = mapping['cases']
        self.assertEqual(len(cases), 12)
        self.assertEqual(sum(row['expected'] == 'accept' for row in cases), 7)
        matches = list(re.finditer(rb'(?m)^TEST_F\(TranslateTest, (\w+)\) \{', self.latest_twelve))
        self.assertEqual(len(matches), 12)
        for match, case in zip(matches, cases):
            self.assertEqual('TranslateTest.' + match[1].decode(), case['formal_test_name'])
            end = self.extractor.body_end(self.latest_twelve, match.end()-1)
            body = self.latest_twelve[match.start():end+1]
            self.assertEqual(hashlib.sha256(body).hexdigest(), case['gtest_body_sha256'])
            begin = body.index(b'R"result_bind(') + len(b'R"result_bind(')
            finish = body.index(b')result_bind"', begin)
            self.assertEqual(hashlib.sha256(body[begin:finish]).hexdigest(), case['source_sha256'])

    def test_latest109_rejects_modified_source_and_mismatched_suffixes(self):
        sources = [self.latest_registered[:-1], self.latest_registered + self.latest_twelve,
                   b' ' + self.latest_registered[1:], self.latest_registered + b' ']
        for source in sources:
            with self.subTest(source=hashlib.sha256(source).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, self.latest_suffix)
        for suffix in [self.additions, self.previous84, self.latest_twelve, self.latest_suffix[:-1]]:
            with self.subTest(suffix=hashlib.sha256(suffix).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(self.latest_registered, suffix)


    def test_query_registry_restores_the_exact_new_source_and_keeps_all_history(self):
        self.assertEqual(self.query_original + self.latest_suffix, self.query_registered)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.query_registered, self.latest_suffix), self.query_original)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.query_original, self.latest_suffix), self.query_original)
        self.assertEqual(self.query_original, self.latest_original + self.query_additions)
        self.assertEqual(self.query_additions.count(b'TEST_F('), 3)

    def test_query_registry_extracts_all_three_permanent_guards(self):
        selected = ['TranslateTest.' + name for name in [
            'CoreV2QueryOnlyConstructorDeclarationsRunAtBothOptimizations',
            'CoreV2QueryOnlyConstructorDeclarationsRequireRuntimeDefinitions',
            'CoreV2QueryOnlyConstructorDeclarationsKeepDefaultSourceChecks']]
        generated, proof = self.extractor.extract(self.query_original, selected)
        self.assertEqual(proof['original_test_names'], 7496)
        self.assertEqual(proof['selected_count'], 3)
        self.assertEqual(generated.count(b'TEST_F('), 3)
        self.assertEqual(set(proof['selected_original_test_bodies']), set(selected))

    def test_query_registry_preserves_all_124_historical_selected_bodies(self):
        manifest = json.loads((ROOT / 'utils/ci/cpp-pair-piecewise-formal-tests.json').read_text())
        selected = manifest['new'] + manifest['regression']
        _, old = self.extractor.extract(self.latest_original, selected)
        _, current = self.extractor.extract(self.query_original, selected)
        self.assertEqual(old['original_test_names'], 7493)
        self.assertEqual(current['original_test_names'], 7496)
        self.assertEqual(old['selected_count'], 124)
        self.assertEqual(current['selected_count'], 124)
        self.assertEqual(old['selected_original_test_bodies'], current['selected_original_test_bodies'])

    def test_query_registry_rejects_modified_source_and_unknown_owner(self):
        selected = ['TranslateTest.CoreV2QueryOnlyConstructorDeclarationsRunAtBothOptimizations']
        for source in [self.query_original + b'\n',
                       self.query_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.query_original.replace(b'return 0;', b'return 1;', 1)]:
            with self.subTest(source=hashlib.sha256(source).hexdigest()):
                self.assertNotEqual(source, self.query_original)
                with self.assertRaises(AssertionError):
                    self.extractor.extract(source, selected)
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.query_original, selected + ['TranslateTest.UnknownQueryOwner'])


    def test_r19_registry_keeps_exact_history_and_six_new_bodies(self):
        expected = ['TranslateTest.' + name for name in [
            'CoreV2ScopedAllocatorIntRunAtBothOptimizations',
            'CoreV2Bitset70RunAtBothOptimizations',
            'CoreV2ScopedAllocatorOwnedConstructorRejected',
            'CoreV2BitsetOwnedCountRejected',
            'CoreV2BitsetOwnedPopcountDeclarationRejected',
            'CoreV2BitsetOwnedPopcountMacroRejected']]
        self.assertEqual(self.r19_manifest['new_gtest_names'], expected)
        generated, proof = self.extractor.extract(self.r19_original, expected)
        self.assertEqual(proof['original_test_names'], 7502)
        self.assertEqual(proof['selected_count'], 6)
        self.assertEqual(generated.count(b'TEST_F('), 6)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.r19_registered, self.r19_suffix), self.r19_original)
        manifest = json.loads((ROOT / 'utils/ci/cpp-pair-piecewise-formal-tests.json').read_text())
        selected = manifest['new'] + manifest['regression']
        _, old = self.extractor.extract(self.query_original, selected)
        _, current = self.extractor.extract(self.r19_original, selected)
        self.assertEqual(old['original_test_names'], 7496)
        self.assertEqual(current['original_test_names'], 7502)
        self.assertEqual(old['selected_original_test_bodies'], current['selected_original_test_bodies'])
        first_extra = self.r19_manifest['new_gtest_names'][4].split('.')[1].encode()
        match, = re.finditer(rb'(?m)^TEST_F\(TranslateTest, ' + re.escape(first_extra) + rb'\) \{', self.r19_original)
        previous_r19 = self.r19_original[:match.start()]
        self.assertEqual(hashlib.sha256(previous_r19).hexdigest(), self.adapter.R19_SCOPED_BITSET_MAINTAINED_SHA256)
        self.assertEqual(self.adapter.restore_original_gtests(previous_r19 + self.r19_suffix, self.r19_suffix), previous_r19)
        _, previous = self.extractor.extract(previous_r19, expected[:4])
        self.assertEqual(previous['original_test_names'], 7500)
        _, current_four = self.extractor.extract(self.r19_original, expected[:4])
        self.assertEqual(previous['selected_original_test_bodies'], current_four['selected_original_test_bodies'])

    def test_r19_tests_keep_actual_programs_optimization_loops_and_check_mode(self):
        selected = [row for row in self.r19_manifest['source_cases']
                    if row['kind'] in ['runtime', 'source-body-guard']]
        self.assertEqual(len(selected), 6)
        for row in selected:
            with self.subTest(name=row['name']):
                program = row['source'].encode()
                self.assertEqual(hashlib.sha256(program).hexdigest(), row['source_sha256'])
                self.assertEqual(self.r19_additions.count(program), 1)
        self.assertEqual(self.r19_additions.count(b'compileGenerated('), 2)
        self.assertEqual(self.r19_additions.count(b'{"-O0", "-O2"}'), 2)
        self.assertEqual(self.r19_additions.count(b'"--check"'), 4)

    def test_r19_registry_rejects_changed_bodies_counts_and_suffixes(self):
        for source, suffix in [
                (self.r19_registered + b' ', self.r19_suffix),
                (self.r19_registered[:-1], self.r19_suffix),
                (self.r19_registered + self.r19_suffix, self.r19_suffix),
                (self.r19_registered, self.r19_suffix + b' ')]:
            with self.subTest(source=hashlib.sha256(source).hexdigest(), suffix=hashlib.sha256(suffix).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, suffix)
        selected = self.r19_manifest['new_gtest_names']
        for source in [self.r19_original + b' ',
                       self.r19_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.r19_original.replace(b'return 0;', b'return 1;', 1)]:
            with self.subTest(source=hashlib.sha256(source).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.extractor.extract(source, selected)
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.r19_original, selected + ['TranslateTest.UnknownR19Owner'])



    def test_valarray_registry_preserves_all_previous_r19_bodies(self):
        selected = self.valarray_manifest['new_gtest_names']
        generated, proof = self.extractor.extract(self.valarray_original, selected)
        self.assertEqual(proof['original_test_names'], 7512)
        self.assertEqual(generated.count(b'TEST_F('), 10)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.valarray_registered, self.valarray_suffix), self.valarray_original)
        self.assertEqual(self.r19_registered,
                         self.valarray_original[:-len(self.valarray_additions)] + self.valarray_suffix)

    def test_valarray_tests_keep_the_exact_native_programs_and_all_guards(self):
        cases = self.valarray_manifest['source_cases']
        self.assertEqual(len(cases), 10)
        for case in cases:
            with self.subTest(case=case['name']):
                source = case['source'].encode()
                self.assertEqual(hashlib.sha256(source).hexdigest(), case['source_sha256'])
                self.assertEqual(self.valarray_additions.count(source), 1)
        self.assertEqual(self.valarray_additions.count(b'compileGenerated('), 6)
        self.assertEqual(self.valarray_additions.count(b'{"-O0", "-O2"}'), 6)
        self.assertEqual(self.valarray_additions.count(b'"--check"'), 4)

    def test_valarray_registry_rejects_changed_bodies_and_suffixes(self):
        for source, suffix in [
                (self.valarray_registered + b' ', self.valarray_suffix),
                (self.valarray_registered[:-1], self.valarray_suffix),
                (self.valarray_registered + self.valarray_suffix, self.valarray_suffix),
                (self.valarray_registered, self.valarray_suffix + b' ')]:
            with self.subTest(size=len(source)):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, suffix)
        for source in [self.valarray_original + b' ',
                       self.valarray_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.valarray_original.replace(b'EXPECT_EQ(', b'EXPECT_NE(', 1)]:
            with self.assertRaises(AssertionError):
                self.extractor.extract(source, self.valarray_manifest['new_gtest_names'])



    def test_forward_list_registry_preserves_all_previous_r19_bodies(self):
        selected = self.forward_list_manifest['new_gtest_names']
        generated, proof = self.extractor.extract(self.forward_list_original, selected)
        self.assertEqual(proof['original_test_names'], 7522)
        self.assertEqual(generated.count(b'TEST_F('), 10)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.forward_list_registered, self.forward_list_suffix), self.forward_list_original)
        self.assertEqual(self.valarray_registered,
                         self.forward_list_original[:-len(self.forward_list_additions)] + self.forward_list_suffix)

    def test_forward_list_tests_keep_the_exact_native_programs_and_all_guards(self):
        cases = self.forward_list_manifest['source_cases']
        self.assertEqual(len(cases), 10)
        for case in cases:
            with self.subTest(case=case['name']):
                source = case['source'].encode()
                self.assertEqual(hashlib.sha256(source).hexdigest(), case['source_sha256'])
                self.assertEqual(self.forward_list_additions.count(source), 1)
        self.assertEqual(self.forward_list_additions.count(b'compileGenerated('), 6)
        self.assertEqual(self.forward_list_additions.count(b'{"-O0", "-O2"}'), 6)
        self.assertEqual(self.forward_list_additions.count(b'"--check"'), 4)

    def test_forward_list_registry_rejects_changed_bodies_and_suffixes(self):
        for source, suffix in [
                (self.forward_list_registered + b' ', self.forward_list_suffix),
                (self.forward_list_registered[:-1], self.forward_list_suffix),
                (self.forward_list_registered + self.forward_list_suffix, self.forward_list_suffix),
                (self.forward_list_registered, self.forward_list_suffix + b' ')]:
            with self.subTest(size=len(source)):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, suffix)
        for source in [self.forward_list_original + b' ',
                       self.forward_list_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.forward_list_original.replace(b'EXPECT_EQ(', b'EXPECT_NE(', 1)]:
            with self.assertRaises(AssertionError):
                self.extractor.extract(source, self.forward_list_manifest['new_gtest_names'])



    def test_list_registry_preserves_all_previous_r19_bodies(self):
        selected = self.list_manifest['new_gtest_names']
        generated, proof = self.extractor.extract(self.list_original, selected)
        self.assertEqual(proof['original_test_names'], 7532)
        self.assertEqual(generated.count(b'TEST_F('), 10)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.list_registered, self.list_suffix), self.list_original)
        self.assertEqual(self.forward_list_registered,
                         self.list_original[:-len(self.list_additions)] + self.list_suffix)

    def test_list_tests_keep_the_exact_native_programs_and_all_guards(self):
        cases = self.list_manifest['source_cases']
        self.assertEqual(len(cases), 10)
        for case in cases:
            with self.subTest(case=case['name']):
                source = case['source'].encode()
                self.assertEqual(hashlib.sha256(source).hexdigest(), case['source_sha256'])
                self.assertEqual(self.list_additions.count(source), 1)
        self.assertEqual(self.list_additions.count(b'compileGenerated('), 6)
        self.assertEqual(self.list_additions.count(b'{"-O0", "-O2"}'), 6)
        self.assertEqual(self.list_additions.count(b'"--check"'), 4)

    def test_list_registry_rejects_changed_bodies_and_suffixes(self):
        for source, suffix in [
                (self.list_registered + b' ', self.list_suffix),
                (self.list_registered[:-1], self.list_suffix),
                (self.list_registered + self.list_suffix, self.list_suffix),
                (self.list_registered, self.list_suffix + b' ')]:
            with self.subTest(size=len(source)):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, suffix)
        for source in [self.list_original + b' ',
                       self.list_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.list_original.replace(b'EXPECT_EQ(', b'EXPECT_NE(', 1)]:
            with self.assertRaises(AssertionError):
                self.extractor.extract(source, self.list_manifest['new_gtest_names'])



    def test_deque_adapter_registry_preserves_all_previous_r19_bodies(self):
        selected = self.deque_adapter_manifest['new_gtest_names']
        generated, proof = self.extractor.extract(self.deque_adapter_original, selected)
        self.assertEqual(proof['original_test_names'], 7542)
        self.assertEqual(generated.count(b'TEST_F('), 10)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.deque_adapter_registered, self.deque_adapter_suffix), self.deque_adapter_original)
        self.assertEqual(self.list_registered,
                         self.deque_adapter_original[:-len(self.deque_adapter_additions)] + self.deque_adapter_suffix)

    def test_deque_adapter_tests_keep_the_exact_native_programs_and_all_guards(self):
        cases = self.deque_adapter_manifest['source_cases']
        self.assertEqual(len(cases), 10)
        for case in cases:
            with self.subTest(case=case['name']):
                source = case['source'].encode()
                self.assertEqual(hashlib.sha256(source).hexdigest(), case['source_sha256'])
                self.assertEqual(self.deque_adapter_additions.count(source), 1)
        self.assertEqual(self.deque_adapter_additions.count(b'compileGenerated('), 6)
        self.assertEqual(self.deque_adapter_additions.count(b'{"-O0", "-O2"}'), 6)
        self.assertEqual(self.deque_adapter_additions.count(b'"--check"'), 4)

    def test_deque_adapter_registry_rejects_changed_bodies_and_suffixes(self):
        for source, suffix in [
                (self.deque_adapter_registered + b' ', self.deque_adapter_suffix),
                (self.deque_adapter_registered[:-1], self.deque_adapter_suffix),
                (self.deque_adapter_registered + self.deque_adapter_suffix, self.deque_adapter_suffix),
                (self.deque_adapter_registered, self.deque_adapter_suffix + b' ')]:
            with self.subTest(size=len(source)):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, suffix)
        for source in [self.deque_adapter_original + b' ',
                       self.deque_adapter_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.deque_adapter_original.replace(b'EXPECT_EQ(', b'EXPECT_NE(', 1)]:
            with self.assertRaises(AssertionError):
                self.extractor.extract(source, self.deque_adapter_manifest['new_gtest_names'])



    def test_deque_registry_preserves_all_previous_r19_bodies(self):
        selected = self.deque_manifest['new_gtest_names']
        generated, proof = self.extractor.extract(self.deque_original, selected)
        self.assertEqual(proof['original_test_names'], 7552)
        self.assertEqual(generated.count(b'TEST_F('), 10)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.deque_registered, self.deque_suffix), self.deque_original)
        self.assertEqual(self.deque_adapter_registered,
                         self.deque_original[:-len(self.deque_additions)] + self.deque_suffix)

    def test_deque_tests_keep_the_exact_native_programs_and_all_guards(self):
        cases = self.deque_manifest['source_cases']
        self.assertEqual(len(cases), 10)
        for case in cases:
            with self.subTest(case=case['name']):
                source = case['source'].encode()
                self.assertEqual(hashlib.sha256(source).hexdigest(), case['source_sha256'])
                self.assertEqual(self.deque_additions.count(source), 1)
        self.assertEqual(self.deque_additions.count(b'compileGenerated('), 6)
        self.assertEqual(self.deque_additions.count(b'{"-O0", "-O2"}'), 6)
        self.assertEqual(self.deque_additions.count(b'"--check"'), 4)

    def test_deque_registry_rejects_changed_bodies_and_suffixes(self):
        for source, suffix in [
                (self.deque_registered + b' ', self.deque_suffix),
                (self.deque_registered[:-1], self.deque_suffix),
                (self.deque_registered + self.deque_suffix, self.deque_suffix),
                (self.deque_registered, self.deque_suffix + b' ')]:
            with self.subTest(size=len(source)):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(source, suffix)
        for source in [self.deque_original + b' ',
                       self.deque_original.replace(b'TEST_F(', b'NOT_TEST_F(', 1),
                       self.deque_original.replace(b'EXPECT_EQ(', b'EXPECT_NE(', 1)]:
            with self.assertRaises(AssertionError):
                self.extractor.extract(source, self.deque_manifest['new_gtest_names'])

    def test_selected_definition_fixture_keeps_exact_current_and_archived_registries(self):
        self.assertEqual(self.definition_original + self.definition_suffix, self.definition_registered)
        self.assertEqual(self.adapter.restore_original_gtests(
            self.definition_original, self.definition_suffix), self.definition_original)
        self.assertEqual(self.adapter.restore_definition_fixture_history(self.definition_registered), self.deque_registered)
        self.assertEqual(self.adapter.restore_definition_fixture_history(self.definition_original), self.deque_original)
        selected = ['TranslateTest.CoreV2QueryOnlyConstructorDeclarationsRunAtBothOptimizations']
        generated, proof = self.extractor.extract(self.definition_original, selected)
        self.assertEqual(proof['original_test_names'], 7552)
        self.assertEqual(proof['selected_count'], 1)
        self.assertIn(b'C::C(int) noexcept {}', generated)
        self.assertIn(b'static_assert(__is_constructible(C, int));', generated)
        self.assertFalse(proof['selected_bodies_modified'])

    def test_selected_definition_fixture_rejects_changed_current_registry_bytes(self):
        for altered in [self.definition_registered + b' ',
                        self.definition_registered.replace(b'C::C(int) noexcept {}', b'C::C(int) noexcept { }', 1),
                        self.definition_registered + self.definition_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered, self.definition_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_definition_fixture_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.definition_registered, self.definition_suffix + b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.definition_original + b' ',
                ['TranslateTest.CoreV2QueryOnlyConstructorDeclarationsRunAtBothOptimizations'])


if __name__ == '__main__':
    unittest.main()

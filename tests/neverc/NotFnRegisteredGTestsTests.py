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
        cls.variant_metadata_layout_registered = cls.registered
        cls.variant_metadata_layout_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.variant_metadata_layout_original = cls.adapter.restore_original_gtests(cls.variant_metadata_layout_registered,cls.variant_metadata_layout_suffix)
        cls.registered = cls.adapter.restore_variant_metadata_layout_history(cls.variant_metadata_layout_registered)
        cls.bitset_wide_and_value_swap_registered = cls.registered
        cls.bitset_wide_and_value_swap_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_wide_and_value_swap_original = cls.adapter.restore_original_gtests(cls.bitset_wide_and_value_swap_registered,cls.bitset_wide_and_value_swap_suffix)
        cls.registered = cls.adapter.restore_bitset_wide_and_value_swap_history(cls.bitset_wide_and_value_swap_registered)
        cls.bitset_text_hash_swap_registered = cls.registered
        cls.bitset_text_hash_swap_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_text_hash_swap_original = cls.adapter.restore_original_gtests(cls.bitset_text_hash_swap_registered,cls.bitset_text_hash_swap_suffix)
        cls.registered = cls.adapter.restore_bitset_text_hash_swap_history(cls.bitset_text_hash_swap_registered)
        cls.bitset_checked_operations_registered = cls.registered
        cls.bitset_checked_operations_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_checked_operations_original = cls.adapter.restore_original_gtests(cls.bitset_checked_operations_registered,cls.bitset_checked_operations_suffix)
        cls.registered = cls.adapter.restore_bitset_checked_operations_history(cls.bitset_checked_operations_registered)
        cls.bitset_shift_registered = cls.registered
        cls.bitset_shift_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_shift_original = cls.adapter.restore_original_gtests(cls.bitset_shift_registered,cls.bitset_shift_suffix)
        cls.registered = cls.adapter.restore_bitset_shift_history(cls.bitset_shift_registered)
        cls.bitset_bitwise_registered = cls.registered
        cls.bitset_bitwise_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_bitwise_original = cls.adapter.restore_original_gtests(cls.bitset_bitwise_registered,cls.bitset_bitwise_suffix)
        cls.registered = cls.adapter.restore_bitset_bitwise_history(cls.bitset_bitwise_registered)
        cls.bitset_zero_whole_registered = cls.registered
        cls.bitset_zero_whole_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_zero_whole_original = cls.adapter.restore_original_gtests(cls.bitset_zero_whole_registered,cls.bitset_zero_whole_suffix)
        cls.registered = cls.adapter.restore_bitset_zero_whole_history(cls.bitset_zero_whole_registered)
        cls.valarray_value_registered = cls.registered
        cls.valarray_value_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.valarray_value_original = cls.adapter.restore_original_gtests(cls.valarray_value_registered,cls.valarray_value_suffix)
        cls.registered = cls.adapter.restore_valarray_value_history(cls.valarray_value_registered)
        cls.list_value_registered = cls.registered
        cls.list_value_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.list_value_original = cls.adapter.restore_original_gtests(cls.list_value_registered,cls.list_value_suffix)
        cls.registered = cls.adapter.restore_list_value_history(cls.list_value_registered)
        cls.deque_positional_emplace_registered = cls.registered
        cls.deque_positional_emplace_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_positional_emplace_original = cls.adapter.restore_original_gtests(cls.deque_positional_emplace_registered,cls.deque_positional_emplace_suffix)
        cls.registered = cls.adapter.restore_deque_positional_emplace_history(cls.deque_positional_emplace_registered)
        cls.deque_range_insert_registered = cls.registered
        cls.deque_range_insert_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_range_insert_original = cls.adapter.restore_original_gtests(cls.deque_range_insert_registered,cls.deque_range_insert_suffix)
        cls.registered = cls.adapter.restore_deque_range_insert_history(cls.deque_range_insert_registered)
        cls.deque_count_insert_registered = cls.registered
        cls.deque_count_insert_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_count_insert_original = cls.adapter.restore_original_gtests(cls.deque_count_insert_registered,cls.deque_count_insert_suffix)
        cls.registered = cls.adapter.restore_deque_count_insert_history(cls.deque_count_insert_registered)
        cls.deque_insert_registered = cls.registered
        cls.deque_insert_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_insert_original = cls.adapter.restore_original_gtests(cls.deque_insert_registered,cls.deque_insert_suffix)
        cls.registered = cls.adapter.restore_deque_insert_history(cls.deque_insert_registered)
        cls.deque_erase_registered = cls.registered
        cls.deque_erase_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_erase_original = cls.adapter.restore_original_gtests(cls.deque_erase_registered,cls.deque_erase_suffix)
        cls.registered = cls.adapter.restore_deque_erase_history(cls.deque_erase_registered)
        cls.deque_initializer_assignment_registered = cls.registered
        cls.deque_initializer_assignment_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_initializer_assignment_original = cls.adapter.restore_original_gtests(cls.deque_initializer_assignment_registered,cls.deque_initializer_assignment_suffix)
        cls.registered = cls.adapter.restore_deque_initializer_assignment_history(cls.deque_initializer_assignment_registered)
        cls.deque_explicit_allocator_registered = cls.registered
        cls.deque_explicit_allocator_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_explicit_allocator_original = cls.adapter.restore_original_gtests(cls.deque_explicit_allocator_registered,cls.deque_explicit_allocator_suffix)
        cls.registered = cls.adapter.restore_deque_explicit_allocator_history(cls.deque_explicit_allocator_registered)
        cls.deque_range_registered = cls.registered
        cls.deque_range_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_range_original = cls.adapter.restore_original_gtests(cls.deque_range_registered,cls.deque_range_suffix)
        cls.registered = cls.adapter.restore_deque_range_history(cls.deque_range_registered)
        cls.deque_count_registered = cls.registered
        cls.deque_count_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_count_original = cls.adapter.restore_original_gtests(cls.deque_count_registered,cls.deque_count_suffix)
        cls.registered = cls.adapter.restore_deque_count_history(cls.deque_count_registered)
        cls.deque_value_registered = cls.registered
        cls.deque_value_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_value_original = cls.adapter.restore_original_gtests(cls.deque_value_registered,cls.deque_value_suffix)
        cls.registered = cls.adapter.restore_deque_value_history(cls.deque_value_registered)
        cls.deque_emplace_registered = cls.registered
        cls.deque_emplace_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.deque_emplace_original = cls.adapter.restore_original_gtests(cls.deque_emplace_registered,cls.deque_emplace_suffix)
        cls.registered = cls.adapter.restore_deque_emplace_history(cls.deque_emplace_registered)
        cls.priority_lessvoid_registered = cls.registered
        cls.priority_lessvoid_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_lessvoid_original = cls.adapter.restore_original_gtests(cls.priority_lessvoid_registered,cls.priority_lessvoid_suffix)
        cls.registered = cls.adapter.restore_priority_lessvoid_history(cls.priority_lessvoid_registered)
        cls.priority_greatervoid_registered = cls.registered
        cls.priority_greatervoid_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_greatervoid_original = cls.adapter.restore_original_gtests(cls.priority_greatervoid_registered,cls.priority_greatervoid_suffix)
        cls.registered = cls.adapter.restore_priority_greatervoid_history(cls.priority_greatervoid_registered)
        cls.priority_greater_registered = cls.registered
        cls.priority_greater_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_greater_original = cls.adapter.restore_original_gtests(cls.priority_greater_registered,cls.priority_greater_suffix)
        cls.registered = cls.adapter.restore_priority_greater_history(cls.priority_greater_registered)
        cls.priority_alloc_registered = cls.registered
        cls.priority_alloc_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_alloc_original = cls.adapter.restore_original_gtests(cls.priority_alloc_registered,cls.priority_alloc_suffix)
        cls.registered = cls.adapter.restore_priority_allocator_history(cls.priority_alloc_registered)
        cls.priority_ctor_registered = cls.registered
        cls.priority_ctor_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_ctor_original = cls.adapter.restore_original_gtests(cls.priority_ctor_registered,cls.priority_ctor_suffix)
        cls.registered = cls.adapter.restore_priority_constructor_history(cls.priority_ctor_registered)
        cls.priority_value_registered = cls.registered
        cls.priority_value_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_value_original = cls.adapter.restore_original_gtests(cls.priority_value_registered,cls.priority_value_suffix)
        cls.registered = cls.adapter.restore_priority_value_history(cls.priority_value_registered)
        cls.priority_registered = cls.registered
        cls.priority_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.priority_original = cls.adapter.restore_original_gtests(cls.priority_registered,cls.priority_suffix)
        cls.registered = cls.adapter.restore_priority_queue_history(cls.priority_registered)
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

    def test_priority_queue_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_original+self.priority_suffix,self.priority_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_original,self.priority_suffix),self.priority_original)
        self.assertEqual(self.adapter.restore_priority_queue_history(self.priority_registered),self.definition_registered)
        self.assertEqual(self.adapter.restore_priority_queue_history(self.priority_original),self.definition_original)
        selected = ['TranslateTest.CoreV2PriorityQueueIntEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntExtremesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAliasedGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntInterleavedRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAllocationReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntArgumentEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueOwnedPushRejected', 'TranslateTest.CoreV2PriorityQueueOwnedTopMacroRejected', 'TranslateTest.CoreV2PriorityQueueOwnedComparatorFieldMacroRejected', 'TranslateTest.CoreV2PriorityQueueOwnedEmptyRejected']
        generated,proof = self.extractor.extract(self.priority_original,selected)
        self.assertEqual(proof['original_test_names'],7562)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7552)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'q.push(q.top())',generated)
        self.assertIn(b'*armed=19',generated)

    def test_priority_queue_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_registered+b' ',
                        self.priority_registered.replace(b'q.push(q.top())',b'q.push(11)',1),
                        self.priority_registered+self.priority_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_queue_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_registered,self.priority_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_original+b' ',['TranslateTest.CoreV2PriorityQueueIntEmptyRunAtBothOptimizations'])

    def test_priority_value_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_value_original+self.priority_value_suffix,self.priority_value_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_value_original,self.priority_value_suffix),self.priority_value_original)
        self.assertEqual(self.adapter.restore_priority_value_history(self.priority_value_registered),self.priority_registered)
        self.assertEqual(self.adapter.restore_priority_value_history(self.priority_value_original),self.priority_original)
        selected = ['TranslateTest.CoreV2PriorityQueueIntCopyConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntCopyAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntMoveConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntMoveAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntSwapIdentitiesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntEmplaceGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueOwnedCopyConstructorRejected', 'TranslateTest.CoreV2PriorityQueueOwnedCopyAssignmentRejected', 'TranslateTest.CoreV2PriorityQueueOwnedSwapRejected', 'TranslateTest.CoreV2PriorityQueueOwnedEmplaceMacroRejected']
        generated,proof = self.extractor.extract(self.priority_value_original,selected)
        self.assertEqual(proof['original_test_names'],7572)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7562)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'q.emplace(q.top())',generated)
        self.assertIn(b'other=other',generated)

    def test_priority_value_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_value_registered+b' ',
                        self.priority_value_registered.replace(b'q.emplace(q.top())',b'q.push(11)',1),
                        self.priority_value_registered+self.priority_value_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_value_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_value_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_value_registered,self.priority_value_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_value_original+b' ',['TranslateTest.CoreV2PriorityQueueIntCopyConstructionRunAtBothOptimizations'])

    def test_priority_constructor_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_ctor_original+self.priority_ctor_suffix,self.priority_ctor_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_ctor_original,self.priority_ctor_suffix),self.priority_ctor_original)
        self.assertEqual(self.adapter.restore_priority_constructor_history(self.priority_ctor_registered),self.priority_value_registered)
        self.assertEqual(self.adapter.restore_priority_constructor_history(self.priority_ctor_original),self.priority_value_original)
        selected = ['TranslateTest.CoreV2PriorityQueueIntComparatorConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntCopyVectorConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntMoveVectorConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntPointerRangeConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntRangeCopyPrefixRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntRangeMovePrefixRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueOwnedComparatorConstructorRejected', 'TranslateTest.CoreV2PriorityQueueOwnedCopyVectorConstructorRejected', 'TranslateTest.CoreV2PriorityQueueOwnedMakeHeapRejected', 'TranslateTest.CoreV2PriorityQueueConstructorOwnedLengthErrorRejected']
        generated,proof = self.extractor.extract(self.priority_ctor_original,selected)
        self.assertEqual(proof['original_test_names'],7582)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7572)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'std::priority_queue<int> q(compare',generated)
        self.assertIn(b'std::move(values)',generated)

    def test_priority_constructor_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_ctor_registered+b' ',
                        self.priority_ctor_registered.replace(b'std::priority_queue<int> q(compare',b'q.push(11)',1),
                        self.priority_ctor_registered+self.priority_ctor_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_ctor_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_constructor_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_ctor_registered,self.priority_ctor_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_ctor_original+b' ',['TranslateTest.CoreV2PriorityQueueIntComparatorConstructionRunAtBothOptimizations'])

    def test_priority_allocator_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_alloc_original+self.priority_alloc_suffix,self.priority_alloc_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_alloc_original,self.priority_alloc_suffix),self.priority_alloc_original)
        self.assertEqual(self.adapter.restore_priority_allocator_history(self.priority_alloc_registered),self.priority_ctor_registered)
        self.assertEqual(self.adapter.restore_priority_allocator_history(self.priority_alloc_original),self.priority_ctor_original)
        selected = ['TranslateTest.CoreV2PriorityQueueIntAllocatorEmptyComparatorRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAllocatorCopyVectorRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAllocatorMoveVectorRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAllocatorCopyMoveQueueRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAllocatorPointerRangeRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueIntAllocatorRangeCopyMovePrefixRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueOwnedAllocatorConstructorRejected', 'TranslateTest.CoreV2PriorityQueueAllocatorOwnedVectorCopyConstructorRejected', 'TranslateTest.CoreV2PriorityQueueAllocatorOwnedMakeHeapRejected', 'TranslateTest.CoreV2PriorityQueueAllocatorOwnedLengthErrorRejected']
        generated,proof = self.extractor.extract(self.priority_alloc_original,selected)
        self.assertEqual(proof['original_test_names'],7592)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7582)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'std::allocator<int> alloc',generated)
        self.assertIn(b'std::move(prefix)',generated)

    def test_priority_allocator_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_alloc_registered+b' ',
                        self.priority_alloc_registered.replace(b'std::allocator<int> alloc',b'q.push(11)',1),
                        self.priority_alloc_registered+self.priority_alloc_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_alloc_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_allocator_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_alloc_registered,self.priority_alloc_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_alloc_original+b' ',['TranslateTest.CoreV2PriorityQueueIntAllocatorEmptyComparatorRunAtBothOptimizations'])

    def test_priority_greater_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_greater_original+self.priority_greater_suffix,self.priority_greater_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_greater_original,self.priority_greater_suffix),self.priority_greater_original)
        self.assertEqual(self.adapter.restore_priority_greater_history(self.priority_greater_registered),self.priority_alloc_registered)
        self.assertEqual(self.adapter.restore_priority_greater_history(self.priority_greater_original),self.priority_alloc_original)
        selected = ['TranslateTest.CoreV2PriorityQueueGreaterIntHeapRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterIntValuesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterIntVectorsRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterIntPointerRangesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterIntRangePrefixesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterIntAllocatorsRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterOwnedComparatorRejected', 'TranslateTest.CoreV2PriorityQueueGreaterOwnedPushRejected', 'TranslateTest.CoreV2PriorityQueueGreaterOwnedMakeHeapRejected', 'TranslateTest.CoreV2PriorityQueueGreaterOwnedLengthErrorRejected']
        generated,proof = self.extractor.extract(self.priority_greater_original,selected)
        self.assertEqual(proof['original_test_names'],7602)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7592)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'using Minimum=',generated)
        self.assertIn(b'std::move(prefix)',generated)

    def test_priority_greater_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_greater_registered+b' ',
                        self.priority_greater_registered.replace(b'using Minimum=',b'q.push(11)',1),
                        self.priority_greater_registered+self.priority_greater_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_greater_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_greater_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_greater_registered,self.priority_greater_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_greater_original+b' ',['TranslateTest.CoreV2PriorityQueueGreaterIntHeapRunAtBothOptimizations'])

    def test_priority_greatervoid_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_greatervoid_original+self.priority_greatervoid_suffix,self.priority_greatervoid_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_greatervoid_original,self.priority_greatervoid_suffix),self.priority_greatervoid_original)
        self.assertEqual(self.adapter.restore_priority_greatervoid_history(self.priority_greatervoid_registered),self.priority_greater_registered)
        self.assertEqual(self.adapter.restore_priority_greatervoid_history(self.priority_greatervoid_original),self.priority_greater_original)
        selected = ['TranslateTest.CoreV2PriorityQueueGreaterVoidHeapRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterVoidValuesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterVoidVectorsRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterVoidPointerRangesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterVoidRangePrefixesRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterVoidAllocatorsRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueGreaterVoidOwnedComparatorRejected', 'TranslateTest.CoreV2PriorityQueueGreaterVoidOwnedPushRejected', 'TranslateTest.CoreV2PriorityQueueGreaterVoidOwnedMakeHeapRejected', 'TranslateTest.CoreV2PriorityQueueGreaterVoidOwnedLengthErrorRejected']
        generated,proof = self.extractor.extract(self.priority_greatervoid_original,selected)
        self.assertEqual(proof['original_test_names'],7612)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7602)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'using Minimum=',generated)
        self.assertIn(b'std::move(prefix)',generated)

    def test_priority_greatervoid_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_greatervoid_registered+b' ',
                        self.priority_greatervoid_registered.replace(b'using Minimum=',b'q.push(11)',1),
                        self.priority_greatervoid_registered+self.priority_greatervoid_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_greatervoid_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_greatervoid_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_greatervoid_registered,self.priority_greatervoid_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_greatervoid_original+b' ',['TranslateTest.CoreV2PriorityQueueGreaterVoidHeapRunAtBothOptimizations'])

    def test_priority_lessvoid_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.priority_lessvoid_original+self.priority_lessvoid_suffix,self.priority_lessvoid_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.priority_lessvoid_original,self.priority_lessvoid_suffix),self.priority_lessvoid_original)
        self.assertEqual(self.adapter.restore_priority_lessvoid_history(self.priority_lessvoid_registered),self.priority_greatervoid_registered)
        self.assertEqual(self.adapter.restore_priority_lessvoid_history(self.priority_lessvoid_original),self.priority_greatervoid_original)
        selected = ['TranslateTest.CoreV2PriorityQueueLessVoidAllocatorEmptyComparatorRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueLessVoidAllocatorCopyVectorRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueLessVoidAllocatorMoveVectorRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueLessVoidAllocatorCopyMoveQueueRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueLessVoidAllocatorPointerRangeRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueLessVoidAllocatorRangeCopyMovePrefixRunAtBothOptimizations', 'TranslateTest.CoreV2PriorityQueueLessVoidOwnedComparatorRejected', 'TranslateTest.CoreV2PriorityQueueLessVoidOwnedVectorCopyRejected', 'TranslateTest.CoreV2PriorityQueueLessVoidOwnedMakeHeapRejected', 'TranslateTest.CoreV2PriorityQueueLessVoidOwnedLengthErrorRejected']
        generated,proof = self.extractor.extract(self.priority_lessvoid_original,selected)
        self.assertEqual(proof['original_test_names'],7622)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7612)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'std::less<void>',generated)
        self.assertIn(b'std::move(prefix)',generated)

    def test_priority_lessvoid_rejects_modified_current_source_and_suffix(self):
        for altered in [self.priority_lessvoid_registered+b' ',
                        self.priority_lessvoid_registered.replace(b'std::less<void>',b'q.push(11)',1),
                        self.priority_lessvoid_registered+self.priority_lessvoid_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.priority_lessvoid_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_priority_lessvoid_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.priority_lessvoid_registered,self.priority_lessvoid_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.priority_lessvoid_original+b' ',['TranslateTest.CoreV2PriorityQueueLessVoidAllocatorEmptyComparatorRunAtBothOptimizations'])

    def test_deque_emplace_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_emplace_original+self.deque_emplace_suffix,self.deque_emplace_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_emplace_original,self.deque_emplace_suffix),self.deque_emplace_original)
        self.assertEqual(self.adapter.restore_deque_emplace_history(self.deque_emplace_registered),self.priority_lessvoid_registered)
        self.assertEqual(self.adapter.restore_deque_emplace_history(self.deque_emplace_original),self.priority_lessvoid_original)
        selected = ['TranslateTest.CoreV2DequeEmplaceValueReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEmplaceBlockBoundariesRunAtBothOptimizations', 'TranslateTest.CoreV2StackEmplaceValueReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2QueueEmplaceValueReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEmplaceLiveAllocationReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEmplaceOwnedArgumentEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEmplaceBackOwnedBodyRejected', 'TranslateTest.CoreV2DequeEmplaceFrontOwnedBodyRejected', 'TranslateTest.CoreV2StackEmplaceOwnedBodyRejected', 'TranslateTest.CoreV2QueueEmplaceOwnedBodyRejected']
        generated,proof = self.extractor.extract(self.deque_emplace_original,selected)
        self.assertEqual(proof['original_test_names'],7632)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7622)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqemplace',generated)
        self.assertIn(b'emplace_front',generated)

    def test_deque_emplace_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_emplace_registered+b' ',
                        self.deque_emplace_registered.replace(b'r19dqemplace',b'q.push(11)',1),
                        self.deque_emplace_registered+self.deque_emplace_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_emplace_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_emplace_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_emplace_registered,self.deque_emplace_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_emplace_original+b' ',['TranslateTest.CoreV2DequeEmplaceValueReferenceRunAtBothOptimizations'])

    def test_deque_value_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_value_original+self.deque_value_suffix,self.deque_value_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_value_original,self.deque_value_suffix),self.deque_value_original)
        self.assertEqual(self.adapter.restore_deque_value_history(self.deque_value_registered),self.deque_emplace_registered)
        self.assertEqual(self.adapter.restore_deque_value_history(self.deque_value_original),self.deque_emplace_original)
        selected = ['TranslateTest.CoreV2DequeValueCopyConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2DequeValueCopyAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2DequeValueMoveConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2DequeValueMoveAssignmentAndSwapRunAtBothOptimizations', 'TranslateTest.CoreV2StackValueCopyMoveAndSwapRunAtBothOptimizations', 'TranslateTest.CoreV2QueueValueCopyMoveAndSwapRunAtBothOptimizations', 'TranslateTest.CoreV2DequeValueCopyConstructorOwnedBodyRejected', 'TranslateTest.CoreV2DequeValueCopyAssignmentOwnedBodyRejected', 'TranslateTest.CoreV2DequeValueMoveConstructorOwnedBodyRejected', 'TranslateTest.CoreV2DequeValueSwapOwnedBodyRejected']
        generated,proof = self.extractor.extract(self.deque_value_original,selected)
        self.assertEqual(proof['original_test_names'],7642)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7632)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqvalue',generated)
        self.assertIn(b'std::move',generated)

    def test_deque_value_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_value_registered+b' ',
                        self.deque_value_registered.replace(b'r19dqvalue',b'q.push(11)',1),
                        self.deque_value_registered+self.deque_value_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_value_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_value_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_value_registered,self.deque_value_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_value_original+b' ',['TranslateTest.CoreV2DequeValueCopyConstructionRunAtBothOptimizations'])

    def test_deque_count_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_count_original+self.deque_count_suffix,self.deque_count_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_count_original,self.deque_count_suffix),self.deque_count_original)
        self.assertEqual(self.adapter.restore_deque_count_history(self.deque_count_registered),self.deque_value_registered)
        self.assertEqual(self.adapter.restore_deque_count_history(self.deque_count_original),self.deque_value_original)
        selected = ['TranslateTest.CoreV2DequeCountDefaultConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountFillConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountResizeDefaultRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountResizeFillRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountAssignFillRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountConstructorOwnedBodyRejected', 'TranslateTest.CoreV2DequeCountFillConstructorOwnedBodyRejected', 'TranslateTest.CoreV2DequeCountResizeOwnedBodiesRejected', 'TranslateTest.CoreV2DequeCountAssignOwnedBodyRejected']
        generated,proof = self.extractor.extract(self.deque_count_original,selected)
        self.assertEqual(proof['original_test_names'],7652)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7642)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqcount',generated)
        self.assertIn(b'resize',generated)

    def test_deque_count_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_count_registered+b' ',
                        self.deque_count_registered.replace(b'r19dqcount',b'q.push(11)',1),
                        self.deque_count_registered+self.deque_count_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_count_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_count_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_count_registered,self.deque_count_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_count_original+b' ',['TranslateTest.CoreV2DequeCountDefaultConstructionRunAtBothOptimizations'])

    def test_deque_range_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_range_original+self.deque_range_suffix,self.deque_range_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_range_original,self.deque_range_suffix),self.deque_range_original)
        self.assertEqual(self.adapter.restore_deque_range_history(self.deque_range_registered),self.deque_count_registered)
        self.assertEqual(self.adapter.restore_deque_range_history(self.deque_range_original),self.deque_count_original)
        selected = ['TranslateTest.CoreV2DequeRangeMutableConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeConstConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeAssignmentGrowRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeAssignmentShrinkRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeConstAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeMutableConstructorOwnedBodyRejected', 'TranslateTest.CoreV2DequeRangeMutableAssignmentOwnedBodyRejected', 'TranslateTest.CoreV2DequeRangeConstConstructorOwnedBodyRejected', 'TranslateTest.CoreV2DequeRangeConstAssignmentOwnedBodyRejected']
        generated,proof = self.extractor.extract(self.deque_range_original,selected)
        self.assertEqual(proof['original_test_names'],7662)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7652)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqrange',generated)
        self.assertIn(b'assign',generated)

    def test_deque_range_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_range_registered+b' ',
                        self.deque_range_registered.replace(b'r19dqrange',b'q.push(11)',1),
                        self.deque_range_registered+self.deque_range_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_range_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_range_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_range_registered,self.deque_range_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_range_original+b' ',['TranslateTest.CoreV2DequeRangeMutableConstructionRunAtBothOptimizations'])

    def test_deque_explicit_allocator_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_explicit_allocator_original+self.deque_explicit_allocator_suffix,self.deque_explicit_allocator_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_explicit_allocator_original,self.deque_explicit_allocator_suffix),self.deque_explicit_allocator_original)
        self.assertEqual(self.adapter.restore_deque_explicit_allocator_history(self.deque_explicit_allocator_registered),self.deque_range_registered)
        self.assertEqual(self.adapter.restore_deque_explicit_allocator_history(self.deque_explicit_allocator_original),self.deque_range_original)
        selected = ['TranslateTest.CoreV2DequeExplicitAllocatorCountRunAtBothOptimizations', 'TranslateTest.CoreV2DequeExplicitAllocatorFillEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeExplicitAllocatorPointerRangesRunAtBothOptimizations', 'TranslateTest.CoreV2DequeExplicitAllocatorCopyRunAtBothOptimizations', 'TranslateTest.CoreV2DequeExplicitAllocatorMoveRunAtBothOptimizations', 'TranslateTest.CoreV2DequeExplicitAllocatorCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeExplicitAllocatorCountOwnedBodyRejected', 'TranslateTest.CoreV2DequeExplicitAllocatorFillOwnedBodyRejected', 'TranslateTest.CoreV2DequeExplicitAllocatorRangeOwnedBodiesRejected', 'TranslateTest.CoreV2DequeExplicitAllocatorCopyMoveOwnedBodiesRejected']
        generated,proof = self.extractor.extract(self.deque_explicit_allocator_original,selected)
        self.assertEqual(proof['original_test_names'],7672)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7662)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqalloc',generated)
        self.assertIn(b'allocator',generated)

    def test_deque_explicit_allocator_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_explicit_allocator_registered+b' ',
                        self.deque_explicit_allocator_registered.replace(b'r19dqalloc',b'q.push(11)',1),
                        self.deque_explicit_allocator_registered+self.deque_explicit_allocator_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_explicit_allocator_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_explicit_allocator_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_explicit_allocator_registered,self.deque_explicit_allocator_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_explicit_allocator_original+b' ',['TranslateTest.CoreV2DequeExplicitAllocatorCountRunAtBothOptimizations'])

    def test_deque_initializer_assignment_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_initializer_assignment_original+self.deque_initializer_assignment_suffix,self.deque_initializer_assignment_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_initializer_assignment_original,self.deque_initializer_assignment_suffix),self.deque_initializer_assignment_original)
        self.assertEqual(self.adapter.restore_deque_initializer_assignment_history(self.deque_initializer_assignment_registered),self.deque_explicit_allocator_registered)
        self.assertEqual(self.adapter.restore_deque_initializer_assignment_history(self.deque_initializer_assignment_original),self.deque_explicit_allocator_original)
        selected = ['TranslateTest.CoreV2DequeInitializerAssignmentBasicRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInitializerAssignmentGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInitializerAssignmentShrinkEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInitializerAssignmentEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInitializerAssignmentPrefixAllocationRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInitializerAssignmentSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInitializerAssignOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeInitializerAssignOwnedForwardingBodyRejected', 'TranslateTest.CoreV2DequeInitializerOperatorOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeInitializerOperatorOwnedForwardingBodyRejected']
        generated,proof = self.extractor.extract(self.deque_initializer_assignment_original,selected)
        self.assertEqual(proof['original_test_names'],7682)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7672)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqinitassign',generated)
        self.assertIn(b'assign',generated)

    def test_deque_initializer_assignment_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_initializer_assignment_registered+b' ',
                        self.deque_initializer_assignment_registered.replace(b'r19dqinitassign',b'q.push(11)',1),
                        self.deque_initializer_assignment_registered+self.deque_initializer_assignment_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_initializer_assignment_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_initializer_assignment_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_initializer_assignment_registered,self.deque_initializer_assignment_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_initializer_assignment_original+b' ',['TranslateTest.CoreV2DequeInitializerAssignmentBasicRunAtBothOptimizations'])

    def test_deque_erase_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_erase_original+self.deque_erase_suffix,self.deque_erase_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_erase_original,self.deque_erase_suffix),self.deque_erase_original)
        self.assertEqual(self.adapter.restore_deque_erase_history(self.deque_erase_registered),self.deque_initializer_assignment_registered)
        self.assertEqual(self.adapter.restore_deque_erase_history(self.deque_erase_original),self.deque_initializer_assignment_original)
        selected = ['TranslateTest.CoreV2DequeEraseSingleRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEraseFrontRangeRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEraseBackRangeRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEraseEmptyAndReuseRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEraseReleaseEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEraseCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeEraseSingleOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeEraseSingleOwnedForwardingBodyRejected', 'TranslateTest.CoreV2DequeEraseRangeOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeEraseRangeOwnedForwardingBodyRejected']
        generated,proof = self.extractor.extract(self.deque_erase_original,selected)
        self.assertEqual(proof['original_test_names'],7692)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7682)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqerase',generated)
        self.assertIn(b'erase',generated)

    def test_deque_erase_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_erase_registered+b' ',
                        self.deque_erase_registered.replace(b'r19dqerase',b'q.push(11)',1),
                        self.deque_erase_registered+self.deque_erase_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_erase_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_erase_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_erase_registered,self.deque_erase_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_erase_original+b' ',['TranslateTest.CoreV2DequeEraseSingleRunAtBothOptimizations'])

    def test_deque_insert_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_insert_original+self.deque_insert_suffix,self.deque_insert_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_insert_original,self.deque_insert_suffix),self.deque_insert_original)
        self.assertEqual(self.adapter.restore_deque_insert_history(self.deque_insert_registered),self.deque_erase_registered)
        self.assertEqual(self.adapter.restore_deque_insert_history(self.deque_insert_original),self.deque_erase_original)
        selected = ['TranslateTest.CoreV2DequeInsertSingleRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInsertFrontGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInsertBackGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInsertAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInsertAllocationEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInsertCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeInsertCopyOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeInsertCopyOwnedForwardingBodyRejected', 'TranslateTest.CoreV2DequeInsertMoveOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeInsertMoveOwnedForwardingBodyRejected']
        generated,proof = self.extractor.extract(self.deque_insert_original,selected)
        self.assertEqual(proof['original_test_names'],7702)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7692)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqinsert',generated)
        self.assertIn(b'insert',generated)

    def test_deque_insert_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_insert_registered+b' ',
                        self.deque_insert_registered.replace(b'r19dqinsert',b'q.push(11)',1),
                        self.deque_insert_registered+self.deque_insert_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_insert_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_insert_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_insert_registered,self.deque_insert_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_insert_original+b' ',['TranslateTest.CoreV2DequeInsertSingleRunAtBothOptimizations'])

    def test_deque_count_insert_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_count_insert_original+self.deque_count_insert_suffix,self.deque_count_insert_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_count_insert_original,self.deque_count_insert_suffix),self.deque_count_insert_original)
        self.assertEqual(self.adapter.restore_deque_count_insert_history(self.deque_count_insert_registered),self.deque_insert_registered)
        self.assertEqual(self.adapter.restore_deque_count_insert_history(self.deque_count_insert_original),self.deque_insert_original)
        selected = ['TranslateTest.CoreV2DequeCountInsertBasicRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountInsertFrontGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountInsertBackGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountInsertAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountInsertAllocationEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountInsertCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeCountInsertOwnedEmptyBodyRejected', 'TranslateTest.CoreV2DequeCountInsertOwnedForwardingBodyRejected', 'TranslateTest.CoreV2DequeCountInsertOwnedCountForwardingBodyRejected', 'TranslateTest.CoreV2DequeCountInsertOwnedConditionalForwardingBodyRejected']
        generated,proof = self.extractor.extract(self.deque_count_insert_original,selected)
        self.assertEqual(proof['original_test_names'],7712)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7702)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqcntinsert',generated)
        self.assertIn(b'deque-count-insert-',generated)
        self.assertIn(b'insert(',generated)

    def test_deque_count_insert_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_count_insert_registered+b' ',
                        self.deque_count_insert_registered.replace(b'r19dqcntinsert',b'q.push(11)',1),
                        self.deque_count_insert_registered+self.deque_count_insert_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_count_insert_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_count_insert_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_count_insert_registered,self.deque_count_insert_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_count_insert_original+b' ',['TranslateTest.CoreV2DequeCountInsertBasicRunAtBothOptimizations'])

    def test_deque_range_insert_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_range_insert_original+self.deque_range_insert_suffix,self.deque_range_insert_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_range_insert_original,self.deque_range_insert_suffix),self.deque_range_insert_original)
        self.assertEqual(self.adapter.restore_deque_range_insert_history(self.deque_range_insert_registered),self.deque_count_insert_registered)
        self.assertEqual(self.adapter.restore_deque_range_insert_history(self.deque_range_insert_original),self.deque_count_insert_original)
        selected = ['TranslateTest.CoreV2DequeRangeInsertBasicRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeInsertFrontGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeInsertBackGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeInsertReuseAndResultsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeInsertAllocationEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeInsertCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequeRangeInsertOwnedMutableBodyRejected', 'TranslateTest.CoreV2DequeRangeInsertOwnedConstBodyRejected', 'TranslateTest.CoreV2DequeRangeInsertOwnedListEmptyBodyRejected', 'TranslateTest.CoreV2DequeRangeInsertOwnedListForwardingBodyRejected']
        generated,proof = self.extractor.extract(self.deque_range_insert_original,selected)
        self.assertEqual(proof['original_test_names'],7722)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7712)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqrnginsert',generated)
        self.assertIn(b'deque-range-insert-',generated)
        self.assertIn(b'insert(',generated)

    def test_deque_range_insert_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_range_insert_registered+b' ',
                        self.deque_range_insert_registered.replace(b'r19dqrnginsert',b'q.push(11)',1),
                        self.deque_range_insert_registered+self.deque_range_insert_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_range_insert_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_range_insert_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_range_insert_registered,self.deque_range_insert_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_range_insert_original+b' ',['TranslateTest.CoreV2DequeRangeInsertBasicRunAtBothOptimizations'])

    def test_deque_positional_emplace_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.deque_positional_emplace_original+self.deque_positional_emplace_suffix,self.deque_positional_emplace_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.deque_positional_emplace_original,self.deque_positional_emplace_suffix),self.deque_positional_emplace_original)
        self.assertEqual(self.adapter.restore_deque_positional_emplace_history(self.deque_positional_emplace_registered),self.deque_range_insert_registered)
        self.assertEqual(self.adapter.restore_deque_positional_emplace_history(self.deque_positional_emplace_original),self.deque_range_insert_original)
        selected = ['TranslateTest.CoreV2DequePositionalEmplaceSingleRunAtBothOptimizations', 'TranslateTest.CoreV2DequePositionalEmplaceFrontGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequePositionalEmplaceBackGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2DequePositionalEmplaceAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2DequePositionalEmplaceAllocationEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2DequePositionalEmplaceCallSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2DequePositionalEmplaceOwnedZeroEmptyBodyRejected', 'TranslateTest.CoreV2DequePositionalEmplaceOwnedZeroForwardingBodyRejected', 'TranslateTest.CoreV2DequePositionalEmplaceOwnedLvalueEmptyBodyRejected', 'TranslateTest.CoreV2DequePositionalEmplaceOwnedRvalueForwardingBodyRejected']
        generated,proof = self.extractor.extract(self.deque_positional_emplace_original,selected)
        self.assertEqual(proof['original_test_names'],7732)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7722)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19dqposemplace',generated)
        self.assertIn(b'deque-positional-emplace-',generated)
        self.assertIn(b'emplace(',generated)

    def test_deque_positional_emplace_rejects_modified_current_source_and_suffix(self):
        for altered in [self.deque_positional_emplace_registered+b' ',
                        self.deque_positional_emplace_registered.replace(b'r19dqposemplace',b'q.push(11)',1),
                        self.deque_positional_emplace_registered+self.deque_positional_emplace_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.deque_positional_emplace_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_deque_positional_emplace_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.deque_positional_emplace_registered,self.deque_positional_emplace_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.deque_positional_emplace_original+b' ',['TranslateTest.CoreV2DequePositionalEmplaceSingleRunAtBothOptimizations'])

    def test_list_value_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.list_value_original+self.list_value_suffix,self.list_value_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.list_value_original,self.list_value_suffix),self.list_value_original)
        self.assertEqual(self.adapter.restore_list_value_history(self.list_value_registered),self.deque_positional_emplace_registered)
        self.assertEqual(self.adapter.restore_list_value_history(self.list_value_original),self.deque_positional_emplace_original)
        selected = ['TranslateTest.CoreV2ListValueConstructorsRunAtBothOptimizations', 'TranslateTest.CoreV2ListValueCopyAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2ListValueMoveAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2ListValueSwapRunAtBothOptimizations', 'TranslateTest.CoreV2ListValueAllocationEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2ListValueExplicitAllocatorSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2ListValueOwnedCopyConstructorRejected', 'TranslateTest.CoreV2ListValueOwnedMoveConstructorRejected', 'TranslateTest.CoreV2ListValueOwnedCopyAssignmentRejected', 'TranslateTest.CoreV2ListValueOwnedMoveAssignmentRejected']
        generated,proof = self.extractor.extract(self.list_value_original,selected)
        self.assertEqual(proof['original_test_names'],7742)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7732)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19listvalue',generated)
        self.assertIn(b'list-value-',generated)
        self.assertIn(b'std::list<int>',generated)

    def test_list_value_rejects_modified_current_source_and_suffix(self):
        for altered in [self.list_value_registered+b' ',
                        self.list_value_registered.replace(b'r19listvalue',b'q.push(11)',1),
                        self.list_value_registered+self.list_value_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.list_value_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_list_value_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.list_value_registered,self.list_value_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.list_value_original+b' ',['TranslateTest.CoreV2ListValueConstructorsRunAtBothOptimizations'])

    def test_valarray_value_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.valarray_value_original+self.valarray_value_suffix,self.valarray_value_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.valarray_value_original,self.valarray_value_suffix),self.valarray_value_original)
        self.assertEqual(self.adapter.restore_valarray_value_history(self.valarray_value_registered),self.list_value_registered)
        self.assertEqual(self.adapter.restore_valarray_value_history(self.valarray_value_original),self.list_value_original)
        selected = ['TranslateTest.CoreV2ValarrayValueConstructorsRunAtBothOptimizations', 'TranslateTest.CoreV2ValarrayValueCopyAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2ValarrayValueMoveAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2ValarrayValueResizeRunAtBothOptimizations', 'TranslateTest.CoreV2ValarrayValueSwapRunAtBothOptimizations', 'TranslateTest.CoreV2ValarrayValueAllocationAndSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2ValarrayValueOwnedCopyConstructorRejected', 'TranslateTest.CoreV2ValarrayValueOwnedMoveAssignmentRejected', 'TranslateTest.CoreV2ValarrayValueOwnedResizeRejected', 'TranslateTest.CoreV2ValarrayValueOwnedSDKSwapRejected']
        generated,proof = self.extractor.extract(self.valarray_value_original,selected)
        self.assertEqual(proof['original_test_names'],7752)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7742)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19valvalue',generated)
        self.assertIn(b'valarray-value-',generated)
        self.assertIn(b'std::valarray<int>',generated)

    def test_valarray_value_rejects_modified_current_source_and_suffix(self):
        for altered in [self.valarray_value_registered+b' ',
                        self.valarray_value_registered.replace(b'r19valvalue',b'q.push(11)',1),
                        self.valarray_value_registered+self.valarray_value_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.valarray_value_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_valarray_value_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.valarray_value_registered,self.valarray_value_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.valarray_value_original+b' ',['TranslateTest.CoreV2ValarrayValueConstructorsRunAtBothOptimizations'])

    def test_bitset_zero_whole_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.bitset_zero_whole_original+self.bitset_zero_whole_suffix,self.bitset_zero_whole_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_zero_whole_original,self.bitset_zero_whole_suffix),self.bitset_zero_whole_original)
        self.assertEqual(self.adapter.restore_bitset_zero_whole_history(self.bitset_zero_whole_registered),self.valarray_value_registered)
        self.assertEqual(self.adapter.restore_bitset_zero_whole_history(self.bitset_zero_whole_original),self.valarray_value_original)
        selected = ['TranslateTest.CoreV2BitsetZeroLayoutRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWholeMasksRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWholeReturnAliasRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWholeUnsignedMaskRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWholeConstCopyRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWholeReceiverEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetZeroOwnedSizeRejected', 'TranslateTest.CoreV2BitsetWholeOwnedSetRejected', 'TranslateTest.CoreV2BitsetWholeOwnedResetRejected', 'TranslateTest.CoreV2BitsetWholeOwnedFlipRejected']
        generated,proof = self.extractor.extract(self.bitset_zero_whole_original,selected)
        self.assertEqual(proof['original_test_names'],7762)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7752)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bitzero',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_zero_whole_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_zero_whole_registered+b' ',
                        self.bitset_zero_whole_registered.replace(b'r19bitzero',b'q.push(11)',1),
                        self.bitset_zero_whole_registered+self.bitset_zero_whole_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_zero_whole_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_zero_whole_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_zero_whole_registered,self.bitset_zero_whole_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_zero_whole_original+b' ',['TranslateTest.CoreV2BitsetZeroLayoutRunAtBothOptimizations'])

    def test_bitset_bitwise_preserves_all_previous_registry_bytes_and_extracts_ten_tests(self):
        self.assertEqual(self.bitset_bitwise_original+self.bitset_bitwise_suffix,self.bitset_bitwise_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_bitwise_original,self.bitset_bitwise_suffix),self.bitset_bitwise_original)
        self.assertEqual(self.adapter.restore_bitset_bitwise_history(self.bitset_bitwise_registered),self.bitset_zero_whole_registered)
        self.assertEqual(self.adapter.restore_bitset_bitwise_history(self.bitset_bitwise_original),self.bitset_zero_whole_original)
        selected = ['TranslateTest.CoreV2BitsetBitwiseZeroRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBitwiseSingleWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBitwiseMultiWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBitwiseWordBoundariesRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBitwiseAssignmentAliasRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBitwiseAssignmentSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBitwiseOwnedEqualRejected', 'TranslateTest.CoreV2BitsetBitwiseOwnedComplementRejected', 'TranslateTest.CoreV2BitsetBitwiseOwnedAndAssignmentRejected', 'TranslateTest.CoreV2BitsetBitwiseOwnedFreeOrRejected']
        generated,proof = self.extractor.extract(self.bitset_bitwise_original,selected)
        self.assertEqual(proof['original_test_names'],7772)
        self.assertEqual(proof['selected_count'],10)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7762)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bitops',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_bitwise_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_bitwise_registered+b' ',
                        self.bitset_bitwise_registered.replace(b'r19bitops',b'q.push(11)',1),
                        self.bitset_bitwise_registered+self.bitset_bitwise_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_bitwise_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_bitwise_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_bitwise_registered,self.bitset_bitwise_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_bitwise_original+b' ',['TranslateTest.CoreV2BitsetBitwiseZeroRunAtBothOptimizations'])

    def test_bitset_shift_preserves_all_previous_registry_bytes_and_extracts_sixteen_tests(self):
        self.assertEqual(self.bitset_shift_original+self.bitset_shift_suffix,self.bitset_shift_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_shift_original,self.bitset_shift_suffix),self.bitset_shift_original)
        self.assertEqual(self.adapter.restore_bitset_shift_history(self.bitset_shift_registered),self.bitset_bitwise_registered)
        self.assertEqual(self.adapter.restore_bitset_shift_history(self.bitset_shift_original),self.bitset_bitwise_original)
        selected = ['TranslateTest.CoreV2BitsetShiftZeroRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetShiftSingleWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetShiftWordBoundariesRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetShiftDynamicAmountsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetShiftAssignmentIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetShiftSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetShiftOwnedLeftRejected', 'TranslateTest.CoreV2BitsetShiftOwnedRightRejected', 'TranslateTest.CoreV2BitsetShiftOwnedLeftAssignmentRejected', 'TranslateTest.CoreV2BitsetShiftOwnedRightAssignmentRejected', 'TranslateTest.CoreV2BitsetShiftOwnedForwardSpecializationRejected', 'TranslateTest.CoreV2BitsetShiftOwnedForwardMacroRejected', 'TranslateTest.CoreV2BitsetShiftOwnedMemmoveRedeclarationRejected', 'TranslateTest.CoreV2BitsetShiftOwnedConstantQueryMacroRejected', 'TranslateTest.CoreV2BitsetShiftOwnedConstantEvaluationMacroRejected', 'TranslateTest.CoreV2BitsetShiftOwnedConstantEvaluationHelperMacroRejected']
        generated,proof = self.extractor.extract(self.bitset_shift_original,selected)
        self.assertEqual(proof['original_test_names'],7788)
        self.assertEqual(proof['selected_count'],16)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7772)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bitshift',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_shift_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_shift_registered+b' ',
                        self.bitset_shift_registered.replace(b'r19bitshift',b'q.push(11)',1),
                        self.bitset_shift_registered+self.bitset_shift_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_shift_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_shift_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_shift_registered,self.bitset_shift_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_shift_original+b' ',['TranslateTest.CoreV2BitsetShiftZeroRunAtBothOptimizations'])

    def test_bitset_checked_operations_preserves_all_previous_registry_bytes_and_extracts_forty_two_tests(self):
        self.assertEqual(self.bitset_checked_operations_original+self.bitset_checked_operations_suffix,self.bitset_checked_operations_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_checked_operations_original,self.bitset_checked_operations_suffix),self.bitset_checked_operations_original)
        self.assertEqual(self.adapter.restore_bitset_checked_operations_history(self.bitset_checked_operations_registered),self.bitset_shift_registered)
        self.assertEqual(self.adapter.restore_bitset_checked_operations_history(self.bitset_checked_operations_original),self.bitset_shift_original)
        selected = ['TranslateTest.CoreV2BitsetReferenceMaskedWordsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetReferenceProxyCopyAliasRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetReferenceAssignmentReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetReferenceConstAccessRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetReferenceSwapAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetReferenceAssignmentSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetUnsignedEmptyReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetUnsignedUnsignedLongLongFullWidthRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetUnsignedUnsignedLongPortableWidthRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetUnsignedPartialWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetUnsignedValueSnapshotRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetUnsignedConstAndTemporaryRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoundedModuloWordBoundaryRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoundedMaskLastWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoundedWrappedSizeRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoundedUnsignedIntPromotionRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoundedSubexpressionOnceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoundedReceiverBeforeArgumentsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetReferenceOwnedMutableSegmentFieldRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedMutableMaskFieldRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedReferenceConstructionRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedMutableCarrierRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedConstCarrierRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedStorageBaseRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedMutableSubscriptRejected', 'TranslateTest.CoreV2BitsetReferenceOwnedConstSubscriptRejected', 'TranslateTest.CoreV2BitsetUnsignedWideUlongRejected', 'TranslateTest.CoreV2BitsetUnsignedWideUllongRejected', 'TranslateTest.CoreV2BitsetUnsignedSpecializedUlongRejected', 'TranslateTest.CoreV2BitsetUnsignedSpecializedUllongRejected', 'TranslateTest.CoreV2BitsetUnsignedInterceptStorageRejected', 'TranslateTest.CoreV2BitsetUnsignedInterceptBaseRejected', 'TranslateTest.CoreV2BitsetUnsignedUnownedUlongRejected', 'TranslateTest.CoreV2BitsetUnsignedUnownedUllongRejected', 'TranslateTest.CoreV2BitsetBoundedPositionUnsignedWithoutProofRejected', 'TranslateTest.CoreV2BitsetBoundedPositionSignedRemainderRejected', 'TranslateTest.CoreV2BitsetBoundedPositionOversizedMaskRejected', 'TranslateTest.CoreV2BitsetBoundedPositionOversizedDivisorRejected', 'TranslateTest.CoreV2BitsetBoundedPositionVariableDivisorRejected', 'TranslateTest.CoreV2BitsetBoundedPositionEmptyBitsetRejected', 'TranslateTest.CoreV2BitsetBoundedPositionBoundaryLiteralRejected', 'TranslateTest.CoreV2BitsetBoundedPositionOwnedTestBodyRejected']
        generated,proof = self.extractor.extract(self.bitset_checked_operations_original,selected)
        self.assertEqual(proof['original_test_names'],7830)
        self.assertEqual(proof['selected_count'],42)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7788)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bitref',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_checked_operations_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_checked_operations_registered+b' ',
                        self.bitset_checked_operations_registered.replace(b'r19bitref',b'q.push(11)',1),
                        self.bitset_checked_operations_registered+self.bitset_checked_operations_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_checked_operations_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_checked_operations_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_checked_operations_registered,self.bitset_checked_operations_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_checked_operations_original+b' ',['TranslateTest.CoreV2BitsetReferenceMaskedWordsRunAtBothOptimizations'])

    def test_bitset_text_hash_swap_preserves_all_previous_registry_bytes_and_extracts_fifty_six_tests(self):
        self.assertEqual(self.bitset_text_hash_swap_original+self.bitset_text_hash_swap_suffix,self.bitset_text_hash_swap_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_text_hash_swap_original,self.bitset_text_hash_swap_suffix),self.bitset_text_hash_swap_original)
        self.assertEqual(self.adapter.restore_bitset_text_hash_swap_history(self.bitset_text_hash_swap_registered),self.bitset_checked_operations_registered)
        self.assertEqual(self.adapter.restore_bitset_text_hash_swap_history(self.bitset_text_hash_swap_original),self.bitset_checked_operations_original)
        selected = ['TranslateTest.CoreV2BitsetHashZeroSizeRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetHashSingleWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetHashXorCancellationRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetHashNonzeroTailRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetHashReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetHashCopiedCarrierRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetLiteralTextDefaultAndTruncationRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetLiteralTextCrossWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetLiteralTextBoundedLengthAndNulRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetLiteralTextCustomCharactersRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetLiteralTextEqualCharactersRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetLiteralTextEmptyInputAndResultRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetToStringShortDefaultDigitsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetToStringLongCrossWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetToStringEmptyReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetToStringEmbeddedNulRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetToStringPersistentCopyRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetToStringDefaultReturnTemplateRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoolSwapProxyFirstRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoolSwapBoolFirstRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoolSwapCopiedAliasRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoolSwapUnchangedOtherWordsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoolSwapCaptureOnceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetBoolSwapReadAfterCaptureRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetHashWordStorageRejected', 'TranslateTest.CoreV2BitsetHashBitsetBaseRejected', 'TranslateTest.CoreV2BitsetHashHashImplementationRejected', 'TranslateTest.CoreV2BitsetHashHashBaseRejected', 'TranslateTest.CoreV2BitsetHashHashCarrierRejected', 'TranslateTest.CoreV2BitsetHashLegacyBaseRejected', 'TranslateTest.CoreV2BitsetHashSpecializedOperatorRejected', 'TranslateTest.CoreV2BitsetHashSpecializedEmptyOperatorRejected', 'TranslateTest.CoreV2BitsetLiteralTextInvalidDigitRejected', 'TranslateTest.CoreV2BitsetLiteralTextInvalidBeyondWidthRejected', 'TranslateTest.CoreV2BitsetLiteralTextDynamicPointerRejected', 'TranslateTest.CoreV2BitsetLiteralTextDynamicLengthRejected', 'TranslateTest.CoreV2BitsetLiteralTextDynamicDigitRejected', 'TranslateTest.CoreV2BitsetLiteralTextWideCharacterRejected', 'TranslateTest.CoreV2BitsetLiteralTextOwnedDecoderRejected', 'TranslateTest.CoreV2BitsetLiteralTextOwnedLengthRejected', 'TranslateTest.CoreV2BitsetToStringWideCharacterRejected', 'TranslateTest.CoreV2BitsetToStringCustomTraitsRejected', 'TranslateTest.CoreV2BitsetToStringCustomAllocatorRejected', 'TranslateTest.CoreV2BitsetToStringExplicitUserImplementationRejected', 'TranslateTest.CoreV2BitsetToStringOwnedRootNameRejected', 'TranslateTest.CoreV2BitsetToStringOwnedBitsetStorageRejected', 'TranslateTest.CoreV2BitsetToStringOwnedBitReferenceRejected', 'TranslateTest.CoreV2BitsetToStringOwnedStringTypeRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardSegmentRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardMaskRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardMakeReferenceRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardReferenceCarrierRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardStorageBaseRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardStorageRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardOverloadNameRejected', 'TranslateTest.CoreV2BitsetBoolSwapGuardOwnedSubscriptRejected']
        generated,proof = self.extractor.extract(self.bitset_text_hash_swap_original,selected)
        self.assertEqual(proof['original_test_names'],7886)
        self.assertEqual(proof['selected_count'],56)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7830)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bithash',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_text_hash_swap_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_text_hash_swap_registered+b' ',
                        self.bitset_text_hash_swap_registered.replace(b'r19bithash',b'q.push(11)',1),
                        self.bitset_text_hash_swap_registered+self.bitset_text_hash_swap_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_text_hash_swap_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_text_hash_swap_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_text_hash_swap_registered,self.bitset_text_hash_swap_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_text_hash_swap_original+b' ',['TranslateTest.CoreV2BitsetHashZeroSizeRunAtBothOptimizations'])

    def test_bitset_wide_and_value_swap_preserves_all_previous_registry_bytes_and_extracts_twenty_seven_tests(self):
        self.assertEqual(self.bitset_wide_and_value_swap_original+self.bitset_wide_and_value_swap_suffix,self.bitset_wide_and_value_swap_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_wide_and_value_swap_original,self.bitset_wide_and_value_swap_suffix),self.bitset_wide_and_value_swap_original)
        self.assertEqual(self.adapter.restore_bitset_wide_and_value_swap_history(self.bitset_wide_and_value_swap_registered),self.bitset_text_hash_swap_registered)
        self.assertEqual(self.adapter.restore_bitset_wide_and_value_swap_history(self.bitset_wide_and_value_swap_original),self.bitset_text_hash_swap_original)
        selected = ['TranslateTest.CoreV2BitsetWideTemporaryEmptyMultiwordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideTemporaryMaxUnsignedWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideTemporaryKnownNarrowResultRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideTemporaryArgumentCalledOnceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideTemporaryMaskFitRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideTemporaryModulusFitRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetValueSwapEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetValueSwapOneWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetValueSwapMultiwordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetValueSwapExactTemplateRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetValueSwapSelfAliasAndEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetValueSwapExistingProxyRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideTemporaryGuardSavedObjectRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardOverflowingWordRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardFactoryValueRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardIndirectReceiverRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardCopyConstructionRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardStorageRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardRangeDependencyRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardOverflowDeclarationRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardCtzMacroRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardCharBitRejected', 'TranslateTest.CoreV2BitsetWideTemporaryGuardCompilerCharBitRejected', 'TranslateTest.CoreV2BitsetValueSwapGuardSdkOperandRejected', 'TranslateTest.CoreV2BitsetValueSwapGuardSdkTemporaryRejected', 'TranslateTest.CoreV2BitsetValueSwapGuardIndirectCallRejected', 'TranslateTest.CoreV2BitsetValueSwapGuardCastCalleeRejected']
        generated,proof = self.extractor.extract(self.bitset_wide_and_value_swap_original,selected)
        self.assertEqual(proof['original_test_names'],7913)
        self.assertEqual(proof['selected_count'],27)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7886)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bitwidechar',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_wide_and_value_swap_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_wide_and_value_swap_registered+b' ',
                        self.bitset_wide_and_value_swap_registered.replace(b'r19bitwidechar',b'q.push(11)',1),
                        self.bitset_wide_and_value_swap_registered+self.bitset_wide_and_value_swap_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_wide_and_value_swap_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_wide_and_value_swap_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_wide_and_value_swap_registered,self.bitset_wide_and_value_swap_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_wide_and_value_swap_original+b' ',['TranslateTest.CoreV2BitsetWideTemporaryEmptyMultiwordRunAtBothOptimizations'])

    def test_variant_metadata_layout_preserves_all_previous_registry_bytes_and_extracts_twenty_tests(self):
        self.assertEqual(self.variant_metadata_layout_original+self.variant_metadata_layout_suffix,self.variant_metadata_layout_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.variant_metadata_layout_original,self.variant_metadata_layout_suffix),self.variant_metadata_layout_original)
        self.assertEqual(self.adapter.restore_variant_metadata_layout_history(self.variant_metadata_layout_registered),self.bitset_wide_and_value_swap_registered)
        self.assertEqual(self.adapter.restore_variant_metadata_layout_history(self.variant_metadata_layout_original),self.bitset_wide_and_value_swap_original)
        selected = ['TranslateTest.CoreV2VariantMetadataSizeAndSentinelRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataConstAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataNestedAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataSizeQualificationRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataWrittenIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutMixedScalarRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutReferenceAndConstRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutPointerAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutDuplicateTypesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutDeclarationIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataRuntimeValueRejected', 'TranslateTest.CoreV2VariantMetadataVariantSizeStorageRejected', 'TranslateTest.CoreV2VariantMetadataSentinelStorageRejected', 'TranslateTest.CoreV2VariantMetadataSourceSpecializationRejected', 'TranslateTest.CoreV2VariantMetadataVolatileAlternativeRejected', 'TranslateTest.CoreV2VariantMetadataDirectVolatileAlternativeRejected', 'TranslateTest.CoreV2VariantLayoutAlteredIndexMemberRejected', 'TranslateTest.CoreV2VariantLayoutUnsupportedLargeFloatRejected', 'TranslateTest.CoreV2VariantLayoutRuntimeRecordRejected']
        generated,proof = self.extractor.extract(self.variant_metadata_layout_original,selected)
        self.assertEqual(proof['original_test_names'],7933)
        self.assertEqual(proof['selected_count'],20)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7913)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19variantlayout',generated)
        self.assertIn(b'variant-layout-',generated)
        self.assertIn(b'std::variant<',generated)

    def test_variant_metadata_layout_rejects_modified_current_source_and_suffix(self):
        for altered in [self.variant_metadata_layout_registered+b' ',
                        self.variant_metadata_layout_registered.replace(b'r19variantlayout',b'q.push(11)',1),
                        self.variant_metadata_layout_registered+self.variant_metadata_layout_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.variant_metadata_layout_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_variant_metadata_layout_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.variant_metadata_layout_registered,self.variant_metadata_layout_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.variant_metadata_layout_original+b' ',['TranslateTest.CoreV2VariantMetadataSizeAndSentinelRunAtBothOptimizations'])


if __name__ == '__main__':
    unittest.main()

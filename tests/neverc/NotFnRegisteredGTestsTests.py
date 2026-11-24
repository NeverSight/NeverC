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
        cls.ordered_map_scalar_and_checked_at_registered = cls.registered
        cls.ordered_map_scalar_and_checked_at_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_map_scalar_and_checked_at_original = cls.adapter.restore_original_gtests(cls.ordered_map_scalar_and_checked_at_registered,
                                                                        cls.ordered_map_scalar_and_checked_at_suffix)
        cls.registered = cls.adapter.restore_ordered_map_scalar_and_checked_at_history(cls.ordered_map_scalar_and_checked_at_registered)
        cls.ordered_map_and_iteration_registered = cls.registered
        cls.ordered_map_and_iteration_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_map_and_iteration_original = cls.adapter.restore_original_gtests(cls.ordered_map_and_iteration_registered,
                                                                        cls.ordered_map_and_iteration_suffix)
        cls.registered = cls.adapter.restore_ordered_map_and_iteration_history(cls.ordered_map_and_iteration_registered)
        cls.ordered_value_input_registered = cls.registered
        cls.ordered_value_input_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_value_input_original = cls.adapter.restore_original_gtests(cls.ordered_value_input_registered,
                                                                        cls.ordered_value_input_suffix)
        cls.registered = cls.adapter.restore_ordered_value_input_history(cls.ordered_value_input_registered)
        cls.ordered_reverse_registered = cls.registered
        cls.ordered_reverse_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_reverse_original = cls.adapter.restore_original_gtests(cls.ordered_reverse_registered,
                                                                        cls.ordered_reverse_suffix)
        cls.registered = cls.adapter.restore_ordered_reverse_history(cls.ordered_reverse_registered)
        cls.ordered_hints_allocator_ownership_registered = cls.registered
        cls.ordered_hints_allocator_ownership_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_hints_allocator_ownership_original = cls.adapter.restore_original_gtests(cls.ordered_hints_allocator_ownership_registered,
                                                                        cls.ordered_hints_allocator_ownership_suffix)
        cls.registered = cls.adapter.restore_ordered_hints_allocator_ownership_history(cls.ordered_hints_allocator_ownership_registered)
        cls.ordered_node_and_value_compare_registered = cls.registered
        cls.ordered_node_and_value_compare_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_node_and_value_compare_original = cls.adapter.restore_original_gtests(cls.ordered_node_and_value_compare_registered,
                                                                        cls.ordered_node_and_value_compare_suffix)
        cls.registered = cls.adapter.restore_ordered_node_and_value_compare_history(cls.ordered_node_and_value_compare_registered)
        cls.ordered_policies_and_iterator_ranges_registered = cls.registered
        cls.ordered_policies_and_iterator_ranges_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_policies_and_iterator_ranges_original = cls.adapter.restore_original_gtests(cls.ordered_policies_and_iterator_ranges_registered,
                                                                        cls.ordered_policies_and_iterator_ranges_suffix)
        cls.registered = cls.adapter.restore_ordered_policies_and_iterator_ranges_history(cls.ordered_policies_and_iterator_ranges_registered)
        cls.ordered_merge_comparison_and_map_emplace_registered = cls.registered
        cls.ordered_merge_comparison_and_map_emplace_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_merge_comparison_and_map_emplace_original = cls.adapter.restore_original_gtests(cls.ordered_merge_comparison_and_map_emplace_registered,
                                                                        cls.ordered_merge_comparison_and_map_emplace_suffix)
        cls.registered = cls.adapter.restore_ordered_merge_comparison_and_map_emplace_history(cls.ordered_merge_comparison_and_map_emplace_registered)
        cls.ordered_list_assignment_and_emplace_registered = cls.registered
        cls.ordered_list_assignment_and_emplace_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_list_assignment_and_emplace_original = cls.adapter.restore_original_gtests(cls.ordered_list_assignment_and_emplace_registered,
                                                                        cls.ordered_list_assignment_and_emplace_suffix)
        cls.registered = cls.adapter.restore_ordered_list_assignment_and_emplace_history(cls.ordered_list_assignment_and_emplace_registered)
        cls.ordered_move_list_range_registered = cls.registered
        cls.ordered_move_list_range_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_move_list_range_original = cls.adapter.restore_original_gtests(cls.ordered_move_list_range_registered,
                                                                        cls.ordered_move_list_range_suffix)
        cls.registered = cls.adapter.restore_ordered_move_list_range_history(cls.ordered_move_list_range_registered)
        cls.ordered_maps_with_mixed_cursors_registered = cls.registered
        cls.ordered_maps_with_mixed_cursors_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_maps_with_mixed_cursors_original = cls.adapter.restore_original_gtests(cls.ordered_maps_with_mixed_cursors_registered,
                                                                        cls.ordered_maps_with_mixed_cursors_suffix)
        cls.registered = cls.adapter.restore_ordered_maps_with_mixed_cursors_history(cls.ordered_maps_with_mixed_cursors_registered)
        cls.ordered_owner_operations_registered = cls.registered
        cls.ordered_owner_operations_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_owner_operations_original = cls.adapter.restore_original_gtests(cls.ordered_owner_operations_registered,
                                                                        cls.ordered_owner_operations_suffix)
        cls.registered = cls.adapter.restore_ordered_owner_operations_history(cls.ordered_owner_operations_registered)
        cls.ordered_set_core_registered = cls.registered
        cls.ordered_set_core_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_set_core_original = cls.adapter.restore_original_gtests(cls.ordered_set_core_registered,
                                                                        cls.ordered_set_core_suffix)
        cls.registered = cls.adapter.restore_ordered_set_core_history(cls.ordered_set_core_registered)
        cls.unordered_set_unsigned_key_registered = cls.registered
        cls.unordered_set_unsigned_key_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_unsigned_key_original = cls.adapter.restore_original_gtests(cls.unordered_set_unsigned_key_registered,
                                                                        cls.unordered_set_unsigned_key_suffix)
        cls.registered = cls.adapter.restore_unordered_set_unsigned_key_history(cls.unordered_set_unsigned_key_registered)
        cls.associative_node_and_observer_registered = cls.registered
        cls.associative_node_and_observer_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.associative_node_and_observer_original = cls.adapter.restore_original_gtests(cls.associative_node_and_observer_registered,
                                                                        cls.associative_node_and_observer_suffix)
        cls.registered = cls.adapter.restore_associative_node_and_observer_history(cls.associative_node_and_observer_registered)
        cls.unordered_multimap_iteration_and_transfer_registered = cls.registered
        cls.unordered_multimap_iteration_and_transfer_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_multimap_iteration_and_transfer_original = cls.adapter.restore_original_gtests(cls.unordered_multimap_iteration_and_transfer_registered,
                                                                        cls.unordered_multimap_iteration_and_transfer_suffix)
        cls.registered = cls.adapter.restore_unordered_multimap_iteration_and_transfer_history(cls.unordered_multimap_iteration_and_transfer_registered)
        cls.unordered_multimap_owners_registered = cls.registered
        cls.unordered_multimap_owners_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_multimap_owners_original = cls.adapter.restore_original_gtests(cls.unordered_multimap_owners_registered,
                                                                        cls.unordered_multimap_owners_suffix)
        cls.registered = cls.adapter.restore_unordered_multimap_owners_history(cls.unordered_multimap_owners_registered)
        cls.unordered_set_member_receivers_registered = cls.registered
        cls.unordered_set_member_receivers_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_member_receivers_original = cls.adapter.restore_original_gtests(cls.unordered_set_member_receivers_registered,
                                                                        cls.unordered_set_member_receivers_suffix)
        cls.registered = cls.adapter.restore_unordered_set_member_receivers_history(cls.unordered_set_member_receivers_registered)
        cls.unordered_set_bucket_receivers_registered = cls.registered
        cls.unordered_set_bucket_receivers_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_bucket_receivers_original = cls.adapter.restore_original_gtests(cls.unordered_set_bucket_receivers_registered,
                                                                        cls.unordered_set_bucket_receivers_suffix)
        cls.registered = cls.adapter.restore_unordered_set_bucket_receivers_history(cls.unordered_set_bucket_receivers_registered)
        cls.unordered_set_node_handles_registered = cls.registered
        cls.unordered_set_node_handles_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_node_handles_original = cls.adapter.restore_original_gtests(cls.unordered_set_node_handles_registered,
                                                                        cls.unordered_set_node_handles_suffix)
        cls.registered = cls.adapter.restore_unordered_set_node_handles_history(cls.unordered_set_node_handles_registered)
        cls.unordered_set_storage_construction_registered = cls.registered
        cls.unordered_set_storage_construction_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_storage_construction_original = cls.adapter.restore_original_gtests(cls.unordered_set_storage_construction_registered,
                                                                        cls.unordered_set_storage_construction_suffix)
        cls.registered = cls.adapter.restore_unordered_set_storage_construction_history(cls.unordered_set_storage_construction_registered)
        cls.unordered_set_observers_registered = cls.registered
        cls.unordered_set_observers_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_observers_original = cls.adapter.restore_original_gtests(cls.unordered_set_observers_registered,
                                                                        cls.unordered_set_observers_suffix)
        cls.registered = cls.adapter.restore_unordered_set_observers_history(cls.unordered_set_observers_registered)
        cls.unordered_set_merge_registered = cls.registered
        cls.unordered_set_merge_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_merge_original = cls.adapter.restore_original_gtests(cls.unordered_set_merge_registered,
                                                                        cls.unordered_set_merge_suffix)
        cls.registered = cls.adapter.restore_unordered_set_merge_history(cls.unordered_set_merge_registered)
        cls.unordered_set_iterator_range_registered = cls.registered
        cls.unordered_set_iterator_range_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_iterator_range_original = cls.adapter.restore_original_gtests(cls.unordered_set_iterator_range_registered,
                                                                        cls.unordered_set_iterator_range_suffix)
        cls.registered = cls.adapter.restore_unordered_set_iterator_range_history(cls.unordered_set_iterator_range_registered)
        cls.unordered_set_list_assignment_registered = cls.registered
        cls.unordered_set_list_assignment_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_list_assignment_original = cls.adapter.restore_original_gtests(cls.unordered_set_list_assignment_registered,
                                                                        cls.unordered_set_list_assignment_suffix)
        cls.registered = cls.adapter.restore_unordered_set_list_assignment_history(cls.unordered_set_list_assignment_registered)
        cls.unordered_set_copy_assignment_registered = cls.registered
        cls.unordered_set_copy_assignment_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_copy_assignment_original = cls.adapter.restore_original_gtests(cls.unordered_set_copy_assignment_registered,
                                                                        cls.unordered_set_copy_assignment_suffix)
        cls.registered = cls.adapter.restore_unordered_set_copy_assignment_history(cls.unordered_set_copy_assignment_registered)
        cls.unordered_set_owner_copy_registered = cls.registered
        cls.unordered_set_owner_copy_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_owner_copy_original = cls.adapter.restore_original_gtests(cls.unordered_set_owner_copy_registered,
                                                                        cls.unordered_set_owner_copy_suffix)
        cls.registered = cls.adapter.restore_unordered_set_owner_copy_history(cls.unordered_set_owner_copy_registered)
        cls.unordered_set_owner_move_registered = cls.registered
        cls.unordered_set_owner_move_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_owner_move_original = cls.adapter.restore_original_gtests(cls.unordered_set_owner_move_registered,
                                                                        cls.unordered_set_owner_move_suffix)
        cls.registered = cls.adapter.restore_unordered_set_owner_move_history(cls.unordered_set_owner_move_registered)
        cls.unordered_set_pointer_construction_registered = cls.registered
        cls.unordered_set_pointer_construction_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_pointer_construction_original = cls.adapter.restore_original_gtests(cls.unordered_set_pointer_construction_registered,
                                                                        cls.unordered_set_pointer_construction_suffix)
        cls.registered = cls.adapter.restore_unordered_set_pointer_construction_history(cls.unordered_set_pointer_construction_registered)
        cls.unordered_set_pointer_range_registered = cls.registered
        cls.unordered_set_pointer_range_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_pointer_range_original = cls.adapter.restore_original_gtests(cls.unordered_set_pointer_range_registered,
                                                                        cls.unordered_set_pointer_range_suffix)
        cls.registered = cls.adapter.restore_unordered_set_pointer_range_history(cls.unordered_set_pointer_range_registered)
        cls.unordered_set_equality_swap_registered = cls.registered
        cls.unordered_set_equality_swap_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_equality_swap_original = cls.adapter.restore_original_gtests(cls.unordered_set_equality_swap_registered,
                                                                        cls.unordered_set_equality_swap_suffix)
        cls.registered = cls.adapter.restore_unordered_set_equality_swap_history(cls.unordered_set_equality_swap_registered)
        cls.unordered_set_emplacement_registered = cls.registered
        cls.unordered_set_emplacement_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_emplacement_original = cls.adapter.restore_original_gtests(cls.unordered_set_emplacement_registered,
                                                                        cls.unordered_set_emplacement_suffix)
        cls.registered = cls.adapter.restore_unordered_set_emplacement_history(cls.unordered_set_emplacement_registered)
        cls.unordered_set_local_iterators_registered = cls.registered
        cls.unordered_set_local_iterators_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_local_iterators_original = cls.adapter.restore_original_gtests(cls.unordered_set_local_iterators_registered,
                                                                        cls.unordered_set_local_iterators_suffix)
        cls.registered = cls.adapter.restore_unordered_set_local_iterators_history(cls.unordered_set_local_iterators_registered)
        cls.unordered_set_equal_range_registered = cls.registered
        cls.unordered_set_equal_range_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_equal_range_original = cls.adapter.restore_original_gtests(cls.unordered_set_equal_range_registered,
                                                                        cls.unordered_set_equal_range_suffix)
        cls.registered = cls.adapter.restore_unordered_set_equal_range_history(cls.unordered_set_equal_range_registered)
        cls.unordered_set_iterator_erase_registered = cls.registered
        cls.unordered_set_iterator_erase_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_iterator_erase_original = cls.adapter.restore_original_gtests(cls.unordered_set_iterator_erase_registered,
                                                                        cls.unordered_set_iterator_erase_suffix)
        cls.registered = cls.adapter.restore_unordered_set_iterator_erase_history(cls.unordered_set_iterator_erase_registered)
        cls.unordered_set_scalar_insertion_registered = cls.registered
        cls.unordered_set_scalar_insertion_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_scalar_insertion_original = cls.adapter.restore_original_gtests(cls.unordered_set_scalar_insertion_registered,
                                                                        cls.unordered_set_scalar_insertion_suffix)
        cls.registered = cls.adapter.restore_unordered_set_scalar_insertion_history(cls.unordered_set_scalar_insertion_registered)
        cls.unordered_set_global_iterators_registered = cls.registered
        cls.unordered_set_global_iterators_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_global_iterators_original = cls.adapter.restore_original_gtests(cls.unordered_set_global_iterators_registered,
                                                                        cls.unordered_set_global_iterators_suffix)
        cls.registered = cls.adapter.restore_unordered_set_global_iterators_history(cls.unordered_set_global_iterators_registered)
        cls.unordered_multiset_modifiers_registered = cls.registered
        cls.unordered_multiset_modifiers_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_multiset_modifiers_original = cls.adapter.restore_original_gtests(cls.unordered_multiset_modifiers_registered,
                                                                        cls.unordered_multiset_modifiers_suffix)
        cls.registered = cls.adapter.restore_unordered_multiset_modifiers_history(cls.unordered_multiset_modifiers_registered)
        cls.any_character_inline_registered = cls.registered
        cls.any_character_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_character_inline_original = cls.adapter.restore_original_gtests(cls.any_character_inline_registered,
                                                                        cls.any_character_inline_suffix)
        cls.registered = cls.adapter.restore_any_character_inline_history(cls.any_character_inline_registered)
        cls.unordered_multiset_values_registered = cls.registered
        cls.unordered_multiset_values_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_multiset_values_original = cls.adapter.restore_original_gtests(cls.unordered_multiset_values_registered,
                                                                        cls.unordered_multiset_values_suffix)
        cls.registered = cls.adapter.restore_unordered_multiset_values_history(cls.unordered_multiset_values_registered)
        cls.any_long_inline_registered = cls.registered
        cls.any_long_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_long_inline_original = cls.adapter.restore_original_gtests(cls.any_long_inline_registered,
                                                                        cls.any_long_inline_suffix)
        cls.registered = cls.adapter.restore_any_long_inline_history(cls.any_long_inline_registered)
        cls.any_narrow_inline_registered = cls.registered
        cls.any_narrow_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_narrow_inline_original = cls.adapter.restore_original_gtests(cls.any_narrow_inline_registered,
                                                                        cls.any_narrow_inline_suffix)
        cls.registered = cls.adapter.restore_any_narrow_inline_history(cls.any_narrow_inline_registered)
        cls.unordered_set_modifiers_registered = cls.registered
        cls.unordered_set_modifiers_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_modifiers_original = cls.adapter.restore_original_gtests(cls.unordered_set_modifiers_registered,
                                                                        cls.unordered_set_modifiers_suffix)
        cls.registered = cls.adapter.restore_unordered_set_modifiers_history(cls.unordered_set_modifiers_registered)
        cls.unordered_set_values_registered = cls.registered
        cls.unordered_set_values_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_set_values_original = cls.adapter.restore_original_gtests(cls.unordered_set_values_registered,
                                                                        cls.unordered_set_values_suffix)
        cls.registered = cls.adapter.restore_unordered_set_values_history(cls.unordered_set_values_registered)
        cls.unordered_bucket_storage_registered = cls.registered
        cls.unordered_bucket_storage_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_bucket_storage_original = cls.adapter.restore_original_gtests(cls.unordered_bucket_storage_registered,
                                                                        cls.unordered_bucket_storage_suffix)
        cls.registered = cls.adapter.restore_unordered_bucket_storage_history(cls.unordered_bucket_storage_registered)
        cls.ordered_comparator_registered = cls.registered
        cls.ordered_comparator_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_comparator_original = cls.adapter.restore_original_gtests(cls.ordered_comparator_registered,
                                                                        cls.ordered_comparator_suffix)
        cls.registered = cls.adapter.restore_ordered_comparator_history(cls.ordered_comparator_registered)
        cls.any_wide_inline_registered = cls.registered
        cls.any_wide_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_wide_inline_original = cls.adapter.restore_original_gtests(cls.any_wide_inline_registered,
                                                                        cls.any_wide_inline_suffix)
        cls.registered = cls.adapter.restore_any_wide_inline_history(cls.any_wide_inline_registered)
        cls.unordered_load_limit_registered = cls.registered
        cls.unordered_load_limit_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_load_limit_original = cls.adapter.restore_original_gtests(cls.unordered_load_limit_registered,
                                                                        cls.unordered_load_limit_suffix)
        cls.registered = cls.adapter.restore_unordered_load_limit_history(cls.unordered_load_limit_registered)
        cls.unordered_lookup_policies_registered = cls.registered
        cls.unordered_lookup_policies_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_lookup_policies_original = cls.adapter.restore_original_gtests(cls.unordered_lookup_policies_registered,
                                                                        cls.unordered_lookup_policies_suffix)
        cls.registered = cls.adapter.restore_unordered_lookup_policies_history(cls.unordered_lookup_policies_registered)
        cls.unordered_empty_capacity_registered = cls.registered
        cls.unordered_empty_capacity_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_empty_capacity_original = cls.adapter.restore_original_gtests(cls.unordered_empty_capacity_registered,
                                                                        cls.unordered_empty_capacity_suffix)
        cls.registered = cls.adapter.restore_unordered_empty_capacity_history(cls.unordered_empty_capacity_registered)
        cls.unordered_empty_load_factor_registered = cls.registered
        cls.unordered_empty_load_factor_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.unordered_empty_load_factor_original = cls.adapter.restore_original_gtests(cls.unordered_empty_load_factor_registered,
                                                                        cls.unordered_empty_load_factor_suffix)
        cls.registered = cls.adapter.restore_unordered_empty_load_factor_history(cls.unordered_empty_load_factor_registered)
        cls.any_float_inline_registered = cls.registered
        cls.any_float_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_float_inline_original = cls.adapter.restore_original_gtests(cls.any_float_inline_registered,
                                                                        cls.any_float_inline_suffix)
        cls.registered = cls.adapter.restore_any_float_inline_history(cls.any_float_inline_registered)
        cls.empty_hash_set_registered = cls.registered
        cls.empty_hash_set_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.empty_hash_set_original = cls.adapter.restore_original_gtests(cls.empty_hash_set_registered,
                                                                        cls.empty_hash_set_suffix)
        cls.registered = cls.adapter.restore_empty_hash_set_history(cls.empty_hash_set_registered)
        cls.any_bool_inline_registered = cls.registered
        cls.any_bool_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_bool_inline_original = cls.adapter.restore_original_gtests(cls.any_bool_inline_registered,
                                                                        cls.any_bool_inline_suffix)
        cls.registered = cls.adapter.restore_any_bool_inline_history(cls.any_bool_inline_registered)
        cls.ordered_max_size_registered = cls.registered
        cls.ordered_max_size_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_max_size_original = cls.adapter.restore_original_gtests(cls.ordered_max_size_registered,
                                                                        cls.ordered_max_size_suffix)
        cls.registered = cls.adapter.restore_ordered_max_size_history(cls.ordered_max_size_registered)
        cls.ordered_empty_owner_registered = cls.registered
        cls.ordered_empty_owner_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_empty_owner_original = cls.adapter.restore_original_gtests(cls.ordered_empty_owner_registered,
                                                                        cls.ordered_empty_owner_suffix)
        cls.registered = cls.adapter.restore_ordered_empty_owner_history(cls.ordered_empty_owner_registered)
        cls.variant_get_early_return_registered = cls.registered
        cls.variant_get_early_return_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.variant_get_early_return_original = cls.adapter.restore_original_gtests(cls.variant_get_early_return_registered,
                                                                        cls.variant_get_early_return_suffix)
        cls.registered = cls.adapter.restore_variant_get_early_return_history(cls.variant_get_early_return_registered)
        cls.any_unsigned_inline_registered = cls.registered
        cls.any_unsigned_inline_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.any_unsigned_inline_original = cls.adapter.restore_original_gtests(cls.any_unsigned_inline_registered,
                                                                        cls.any_unsigned_inline_suffix)
        cls.registered = cls.adapter.restore_any_unsigned_inline_history(cls.any_unsigned_inline_registered)
        cls.variant_guarded_get_registered = cls.registered
        cls.variant_guarded_get_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.variant_guarded_get_original = cls.adapter.restore_original_gtests(cls.variant_guarded_get_registered,
                                                                        cls.variant_guarded_get_suffix)
        cls.registered = cls.adapter.restore_variant_guarded_get_history(cls.variant_guarded_get_registered)
        cls.monostate_adapter_hash_registered = cls.registered
        cls.monostate_adapter_hash_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.monostate_adapter_hash_original = cls.adapter.restore_original_gtests(cls.monostate_adapter_hash_registered,
                                                                        cls.monostate_adapter_hash_suffix)
        cls.registered = cls.adapter.restore_monostate_adapter_hash_history(cls.monostate_adapter_hash_registered)
        cls.ordered_observer_query_registered = cls.registered
        cls.ordered_observer_query_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.ordered_observer_query_original = cls.adapter.restore_original_gtests(cls.ordered_observer_query_registered,
                                                                        cls.ordered_observer_query_suffix)
        cls.registered = cls.adapter.restore_ordered_observer_query_history(cls.ordered_observer_query_registered)
        cls.layout_variant_visit_alternatives_registered = cls.registered
        cls.layout_variant_visit_alternatives_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_visit_alternatives_original = cls.adapter.restore_original_gtests(cls.layout_variant_visit_alternatives_registered,
                                                                        cls.layout_variant_visit_alternatives_suffix)
        cls.registered = cls.adapter.restore_layout_variant_visit_alternatives_history(cls.layout_variant_visit_alternatives_registered)
        cls.layout_variant_monostate_hash_registered = cls.registered
        cls.layout_variant_monostate_hash_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_monostate_hash_original = cls.adapter.restore_original_gtests(cls.layout_variant_monostate_hash_registered,
                                                                        cls.layout_variant_monostate_hash_suffix)
        cls.registered = cls.adapter.restore_layout_variant_monostate_hash_history(cls.layout_variant_monostate_hash_registered)
        cls.layout_variant_monostate_registered = cls.registered
        cls.layout_variant_monostate_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_monostate_original = cls.adapter.restore_original_gtests(cls.layout_variant_monostate_registered,
                                                                        cls.layout_variant_monostate_suffix)
        cls.registered = cls.adapter.restore_layout_variant_monostate_history(cls.layout_variant_monostate_registered)
        cls.layout_variant_ordering_registered = cls.registered
        cls.layout_variant_ordering_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_ordering_original = cls.adapter.restore_original_gtests(cls.layout_variant_ordering_registered,
                                                                        cls.layout_variant_ordering_suffix)
        cls.registered = cls.adapter.restore_layout_variant_ordering_history(cls.layout_variant_ordering_registered)
        cls.layout_variant_any_allocator_equality_registered = cls.registered
        cls.layout_variant_any_allocator_equality_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_any_allocator_equality_original = cls.adapter.restore_original_gtests(cls.layout_variant_any_allocator_equality_registered,
                                                                        cls.layout_variant_any_allocator_equality_suffix)
        cls.registered = cls.adapter.restore_layout_variant_any_allocator_equality_history(cls.layout_variant_any_allocator_equality_registered)
        cls.layout_variant_any_allocator_registered = cls.registered
        cls.layout_variant_any_allocator_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_any_allocator_original = cls.adapter.restore_original_gtests(cls.layout_variant_any_allocator_registered,
                                                                        cls.layout_variant_any_allocator_suffix)
        cls.registered = cls.adapter.restore_layout_variant_any_allocator_history(cls.layout_variant_any_allocator_registered)
        cls.layout_variant_any_tuple_visitor_registered = cls.registered
        cls.layout_variant_any_tuple_visitor_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_any_tuple_visitor_original = cls.adapter.restore_original_gtests(cls.layout_variant_any_tuple_visitor_registered,
                                                                        cls.layout_variant_any_tuple_visitor_suffix)
        cls.registered = cls.adapter.restore_layout_variant_any_tuple_visitor_history(cls.layout_variant_any_tuple_visitor_registered)
        cls.layout_two_variant_any_tuple_registered = cls.registered
        cls.layout_two_variant_any_tuple_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_two_variant_any_tuple_original = cls.adapter.restore_original_gtests(cls.layout_two_variant_any_tuple_registered,
                                                                        cls.layout_two_variant_any_tuple_suffix)
        cls.registered = cls.adapter.restore_layout_two_variant_any_tuple_history(cls.layout_two_variant_any_tuple_registered)
        cls.layout_two_variant_any_registered = cls.registered
        cls.layout_two_variant_any_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_two_variant_any_original = cls.adapter.restore_original_gtests(cls.layout_two_variant_any_registered,
                                                                        cls.layout_two_variant_any_suffix)
        cls.registered = cls.adapter.restore_layout_two_variant_any_history(cls.layout_two_variant_any_registered)
        cls.layout_any_integer_registered = cls.registered
        cls.layout_any_integer_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_any_integer_original = cls.adapter.restore_original_gtests(cls.layout_any_integer_registered,
                                                                        cls.layout_any_integer_suffix)
        cls.registered = cls.adapter.restore_layout_any_integer_history(cls.layout_any_integer_registered)
        cls.layout_integral_visit_registered = cls.registered
        cls.layout_integral_visit_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_integral_visit_original = cls.adapter.restore_original_gtests(cls.layout_integral_visit_registered,
                                                                        cls.layout_integral_visit_suffix)
        cls.registered = cls.adapter.restore_layout_integral_visit_history(cls.layout_integral_visit_registered)
        cls.layout_assignment_visit_registered = cls.registered
        cls.layout_assignment_visit_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_assignment_visit_original = cls.adapter.restore_original_gtests(cls.layout_assignment_visit_registered,
                                                                        cls.layout_assignment_visit_suffix)
        cls.registered = cls.adapter.restore_layout_assignment_visit_history(cls.layout_assignment_visit_registered)
        cls.layout_adapter_registered = cls.registered
        cls.layout_adapter_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_adapter_original = cls.adapter.restore_original_gtests(cls.layout_adapter_registered,
                                                                        cls.layout_adapter_suffix)
        cls.registered = cls.adapter.restore_layout_adapter_history(cls.layout_adapter_registered)
        cls.layout_variant_operations_registered = cls.registered
        cls.layout_variant_operations_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_variant_operations_original = cls.adapter.restore_original_gtests(
            cls.layout_variant_operations_registered, cls.layout_variant_operations_suffix)
        cls.registered = cls.adapter.restore_layout_variant_operations_history(cls.layout_variant_operations_registered)
        cls.layout_in_place_any_registered = cls.registered
        cls.layout_in_place_any_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_in_place_any_original = cls.adapter.restore_original_gtests(
            cls.layout_in_place_any_registered, cls.layout_in_place_any_suffix)
        cls.registered = cls.adapter.restore_layout_in_place_any_history(cls.layout_in_place_any_registered)
        cls.layout_in_place_registered = cls.registered
        cls.layout_in_place_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.layout_in_place_original = cls.adapter.restore_original_gtests(
            cls.layout_in_place_registered, cls.layout_in_place_suffix)
        cls.registered = cls.adapter.restore_layout_in_place_history(cls.layout_in_place_registered)
        cls.variant_scalar_registered = cls.registered
        cls.variant_scalar_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.variant_scalar_original = cls.adapter.restore_original_gtests(cls.variant_scalar_registered,cls.variant_scalar_suffix)
        cls.registered = cls.adapter.restore_variant_scalar_history(cls.variant_scalar_registered)
        cls.variant_metadata_layout_registered = cls.registered
        cls.variant_metadata_layout_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.variant_metadata_layout_original = cls.adapter.restore_original_gtests(cls.variant_metadata_layout_registered,cls.variant_metadata_layout_suffix)
        cls.registered = cls.adapter.restore_variant_metadata_layout_history(cls.variant_metadata_layout_registered)
        cls.bitset_text_wide_conversion_registered = cls.registered
        cls.bitset_text_wide_conversion_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_text_wide_conversion_original = cls.adapter.restore_original_gtests(cls.bitset_text_wide_conversion_registered,cls.bitset_text_wide_conversion_suffix)
        cls.registered = cls.adapter.restore_bitset_text_wide_conversion_history(cls.bitset_text_wide_conversion_registered)
        cls.bitset_composed_text_index_registered = cls.registered
        cls.bitset_composed_text_index_suffix = (ROOT / 'utils/ci/stl-one-hundred-nine-formal-additions.cpp').read_bytes()
        cls.bitset_composed_text_index_original = cls.adapter.restore_original_gtests(cls.bitset_composed_text_index_registered,cls.bitset_composed_text_index_suffix)
        cls.registered = cls.adapter.restore_bitset_composed_text_index_history(cls.bitset_composed_text_index_registered)
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

    def test_bitset_composed_text_index_preserves_all_previous_registry_bytes_and_extracts_seventy_three_tests(self):
        self.assertEqual(self.bitset_composed_text_index_original+self.bitset_composed_text_index_suffix,self.bitset_composed_text_index_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_composed_text_index_original,self.bitset_composed_text_index_suffix),self.bitset_composed_text_index_original)
        self.assertEqual(self.adapter.restore_bitset_composed_text_index_history(self.bitset_composed_text_index_registered),self.bitset_wide_and_value_swap_registered)
        self.assertEqual(self.adapter.restore_bitset_composed_text_index_history(self.bitset_composed_text_index_original),self.bitset_wide_and_value_swap_original)
        selected = ['TranslateTest.CoreV2BitsetConstLocalEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstLocalSingleWordFitRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstLocalWideUnsignedArgumentRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstLocalOneTimeInitializationRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstLocalLiveSourceIndependenceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstLocalConstAliasTypeRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstantPositionNamespaceConstantRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstantPositionArithmeticRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstantPositionEnumIndexRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstantPositionTypeSizeIndexRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstantPositionConstexprFunctionRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstantPositionReceiverAndValueRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetIndexTypeLastByteIndexRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetIndexTypeWordBoundariesRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetIndexTypeBooleanIndexRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetIndexTypeResetAndFlipRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetIndexTypeUnsignedPointerLoadRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetIndexTypeIndexCalledOnceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetOwnedLiteralDefaultDigitsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetOwnedLiteralOffsetAndCountRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetOwnedLiteralAllocatedStringCrossesWordRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetOwnedLiteralZeroAsADigitRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetOwnedLiteralEmptyInputRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetOwnedLiteralEqualDigitsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstexprTextPointerLocalRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstexprTextPointerNamespaceRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstexprTextPointerFirstNullRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstexprTextPointerCustomDigitsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstexprTextPointerPointerTypeAliasRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstexprTextPointerZeroCountRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetWideSwapAndCheckedPositionsRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetConstLocalGuardMutableLocalRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardAliasToMutableRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardConstReferenceRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardStaticConstRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardConstParameterRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardCopyInitializedRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardConstMemberRejected', 'TranslateTest.CoreV2BitsetConstLocalGuardConditionalReceiverRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardNegativeRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardOnePastRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardConvertedTooLargeRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardEmptyRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardConstantContextOnlyRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardIndexWithEffectRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardConstexprCallWithRuntimeInputRejected', 'TranslateTest.CoreV2BitsetConstantPositionGuardSourceBodyRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardSignedByteRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardTooShortForByteRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardTooShortForBoolRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardOffsetAfterPromotionRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardWideUnsignedRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardExplicitSizeConversionRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardEnumOperandRejected', 'TranslateTest.CoreV2BitsetIndexTypeGuardOwnedSdkBodyRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardNamedStringRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardFactoryStringRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardOutOfRangeOffsetRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardRuntimeOffsetRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardInvalidSelectedDigitRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardNonliteralConstructorInputRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardNonCharTraitsRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardSdkBodyRejected', 'TranslateTest.CoreV2BitsetOwnedLiteralGuardRepresentationQueryRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardMutablePointerRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardMutableGlobalRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardPointerArithmeticRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardNullRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardIndirectInitializerRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardQualifiedWithoutConstexprRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardInvalidBeyondWidthRejected', 'TranslateTest.CoreV2BitsetConstexprTextPointerGuardOwnedDecoderRejected', 'TranslateTest.CoreV2BitsetWideSwapAndOutOfBoundsPositionRejected']
        generated,proof = self.extractor.extract(self.bitset_composed_text_index_original,selected)
        self.assertEqual(proof['original_test_names'],7986)
        self.assertEqual(proof['selected_count'],73)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7913)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bitcomposed',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_composed_text_index_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_composed_text_index_registered+b' ',
                        self.bitset_composed_text_index_registered.replace(b'r19bitcomposed',b'q.push(11)',1),
                        self.bitset_composed_text_index_registered+self.bitset_composed_text_index_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_composed_text_index_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_composed_text_index_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_composed_text_index_registered,self.bitset_composed_text_index_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_composed_text_index_original+b' ',['TranslateTest.CoreV2BitsetConstLocalEmptyRunAtBothOptimizations'])

    def test_bitset_text_wide_conversion_preserves_all_previous_registry_bytes_and_extracts_twelve_tests(self):
        self.assertEqual(self.bitset_text_wide_conversion_original+self.bitset_text_wide_conversion_suffix,self.bitset_text_wide_conversion_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.bitset_text_wide_conversion_original,self.bitset_text_wide_conversion_suffix),self.bitset_text_wide_conversion_original)
        self.assertEqual(self.adapter.restore_bitset_text_wide_conversion_history(self.bitset_text_wide_conversion_registered),self.bitset_composed_text_index_registered)
        self.assertEqual(self.adapter.restore_bitset_text_wide_conversion_history(self.bitset_text_wide_conversion_original),self.bitset_composed_text_index_original)
        selected = ['TranslateTest.CoreV2BitsetTextWideConversionUnsignedLongWidthRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetTextWideConversionUnsignedLongLongWidthRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetTextWideConversionConstexprPointerRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetTextWideConversionOwningSubstringRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetTextWideConversionEmptyOwningRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetTextWideConversionZeroUpperStorageRunAtBothOptimizations', 'TranslateTest.CoreV2BitsetTextWideConversionAboveUnsignedLongRejected', 'TranslateTest.CoreV2BitsetTextWideConversionAboveUnsignedLongLongRejected', 'TranslateTest.CoreV2BitsetTextWideConversionHighWordFromCustomDigitsRejected', 'TranslateTest.CoreV2BitsetTextWideConversionMutableObjectRejected', 'TranslateTest.CoreV2BitsetTextWideConversionRuntimeTextRejected', 'TranslateTest.CoreV2BitsetTextWideConversionInvalidDigitInSelectedRangeRejected']
        generated,proof = self.extractor.extract(self.bitset_text_wide_conversion_original,selected)
        self.assertEqual(proof['original_test_names'],7998)
        self.assertEqual(proof['selected_count'],12)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7986)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19bittextwide',generated)
        self.assertIn(b'bitset-',generated)
        self.assertIn(b'std::bitset<',generated)

    def test_bitset_text_wide_conversion_rejects_modified_current_source_and_suffix(self):
        for altered in [self.bitset_text_wide_conversion_registered+b' ',
                        self.bitset_text_wide_conversion_registered.replace(b'r19bittextwide',b'q.push(11)',1),
                        self.bitset_text_wide_conversion_registered+self.bitset_text_wide_conversion_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.bitset_text_wide_conversion_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_bitset_text_wide_conversion_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.bitset_text_wide_conversion_registered,self.bitset_text_wide_conversion_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.bitset_text_wide_conversion_original+b' ',['TranslateTest.CoreV2BitsetTextWideConversionUnsignedLongWidthRunAtBothOptimizations'])

    def test_variant_metadata_layout_preserves_all_previous_registry_bytes_and_extracts_twenty_tests(self):
        self.assertEqual(self.variant_metadata_layout_original+self.variant_metadata_layout_suffix,self.variant_metadata_layout_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.variant_metadata_layout_original,self.variant_metadata_layout_suffix),self.variant_metadata_layout_original)
        self.assertEqual(self.adapter.restore_variant_metadata_layout_history(self.variant_metadata_layout_registered),self.bitset_text_wide_conversion_registered)
        self.assertEqual(self.adapter.restore_variant_metadata_layout_history(self.variant_metadata_layout_original),self.bitset_text_wide_conversion_original)
        selected = ['TranslateTest.CoreV2VariantMetadataSizeAndSentinelRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataConstAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataNestedAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataSizeQualificationRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataWrittenIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutMixedScalarRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutReferenceAndConstRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutPointerAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutDuplicateTypesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantLayoutDeclarationIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMetadataRuntimeValueRejected', 'TranslateTest.CoreV2VariantMetadataVariantSizeStorageRejected', 'TranslateTest.CoreV2VariantMetadataSentinelStorageRejected', 'TranslateTest.CoreV2VariantMetadataSourceSpecializationRejected', 'TranslateTest.CoreV2VariantMetadataVolatileAlternativeRejected', 'TranslateTest.CoreV2VariantMetadataDirectVolatileAlternativeRejected', 'TranslateTest.CoreV2VariantLayoutAlteredIndexMemberRejected', 'TranslateTest.CoreV2VariantLayoutUnsupportedLargeFloatRejected', 'TranslateTest.CoreV2VariantLayoutRuntimeRecordRejected']
        generated,proof = self.extractor.extract(self.variant_metadata_layout_original,selected)
        self.assertEqual(proof['original_test_names'],8018)
        self.assertEqual(proof['selected_count'],20)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),7998)
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

    def test_variant_scalar_preserves_all_previous_registry_bytes_and_extracts_twenty_five_tests(self):
        self.assertEqual(self.variant_scalar_original+self.variant_scalar_suffix,self.variant_scalar_registered)
        self.assertEqual(self.adapter.restore_original_gtests(self.variant_scalar_original,self.variant_scalar_suffix),self.variant_scalar_original)
        self.assertEqual(self.adapter.restore_variant_scalar_history(self.variant_scalar_registered),self.variant_metadata_layout_registered)
        self.assertEqual(self.adapter.restore_variant_scalar_history(self.variant_scalar_original),self.variant_metadata_layout_original)
        selected = ['TranslateTest.CoreV2VariantObjectsDefaultRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsSecondRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsThirdLvalueRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsCopyRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsDefaultPointerRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsRefAndSideEffectRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsTemporaryRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsGetIfAliasRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsGetIfConstRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsGetIfDuplicateRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsGetIfCopyAndEvaluateOnceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsHoldsConstRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsCopyChangingAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsMoveAndSelfAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsConstGetByIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsConstGetByTypeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantObjectsClassAlternativeRejected', 'TranslateTest.CoreV2VariantObjectsConstAlternativeRejected', 'TranslateTest.CoreV2VariantObjectsAlteredDiscriminantRejected', 'TranslateTest.CoreV2VariantObjectsUnprovedConversionRejected', 'TranslateTest.CoreV2VariantObjectsUncheckedGetRejected', 'TranslateTest.CoreV2VariantObjectsConstGetInactiveRejected', 'TranslateTest.CoreV2VariantObjectsConstGetMutableAliasRejected', 'TranslateTest.CoreV2VariantObjectsConstGetAlteredHelperRejected', 'TranslateTest.CoreV2VariantObjectsGetIfReplacedAddressBodyRejected']
        generated,proof = self.extractor.extract(self.variant_scalar_original,selected)
        self.assertEqual(proof['original_test_names'],8043)
        self.assertEqual(proof['selected_count'],25)
        self.assertEqual(len(proof['nonselected_original_test_bodies']),8018)
        self.assertFalse(proof['selected_bodies_modified'])
        self.assertIn(b'r19variantsclr',generated)
        self.assertIn(b'variant-objects-',generated)
        self.assertIn(b'std::variant<',generated)

    def test_variant_scalar_rejects_modified_current_source_and_suffix(self):
        for altered in [self.variant_scalar_registered+b' ',
                        self.variant_scalar_registered.replace(b'r19variantsclr',b'q.push(11)',1),
                        self.variant_scalar_registered+self.variant_scalar_suffix]:
            with self.subTest(source=hashlib.sha256(altered).hexdigest()):
                with self.assertRaises(AssertionError):
                    self.adapter.restore_original_gtests(altered,self.variant_scalar_suffix)
                with self.assertRaises(AssertionError):
                    self.adapter.restore_variant_scalar_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.variant_scalar_registered,self.variant_scalar_suffix+b' ')
        with self.assertRaises(AssertionError):
            self.extractor.extract(self.variant_scalar_original+b' ',['TranslateTest.CoreV2VariantObjectsDefaultRunAtBothOptimizations'])


    def test_layout_in_place_preserves_earlier_fixtures_and_extracts_added_cases(self):
        self.assertEqual(self.layout_in_place_original + self.layout_in_place_suffix,
                         self.layout_in_place_registered)
        self.assertEqual(self.adapter.restore_layout_in_place_history(self.layout_in_place_original),
                         self.variant_scalar_original)
        generated, proof = self.extractor.extract(self.layout_in_place_original, ['TranslateTest.CoreV2VariantMonostateIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateStandaloneLayoutRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateVariantLayoutRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateMonostateOnlyVariantRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLayoutDirectSizeAlignRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLayoutConstReferenceSizeRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLayoutWrappedPointerTypeRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLayoutConstantArrayBoundRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMapIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMapAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMapSizeofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMapAlignofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeSetIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeSetAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeSetSizeofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeSetAlignofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedSetIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedSetAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedSetSizeofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedSetAlignofRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateValueActiveEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateValueConstAddressRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateValueCopyAndAssignRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateValueDuplicateEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIndexConstructionRepeatedTypeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIndexConstructionZeroInitializedRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIndexConstructionArgumentEffectRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIndexConstructionEmptyNonfirstRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTypeConstructionValueAndEffectRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTypeConstructionValueInitializePointerRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTypeConstructionEmptyAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTypeConstructionZeroIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateSourceRedeclarationRejected', 'TranslateTest.CoreV2VariantMonostateVolatileLayoutRejected', 'TranslateTest.CoreV2AnyLayoutVolatileTypeRejected', 'TranslateTest.CoreV2AnyLayoutRuntimeConstructionRejected', 'TranslateTest.CoreV2AnyLayoutUnevaluatedObjectRejected', 'TranslateTest.CoreV2AnyLayoutAlteredHandlerFieldRejected', 'TranslateTest.CoreV2AssociativeMapCustomTypeOrPolicyRejected', 'TranslateTest.CoreV2AssociativeMapRenamedContainerStorageRejected', 'TranslateTest.CoreV2AssociativeMapRuntimeStorageGuardRejected', 'TranslateTest.CoreV2AssociativeSetCustomTypeOrPolicyRejected', 'TranslateTest.CoreV2AssociativeSetRenamedContainerStorageRejected', 'TranslateTest.CoreV2AssociativeSetRuntimeStorageGuardRejected', 'TranslateTest.CoreV2AssociativeUnorderedSetCustomTypeOrPolicyRejected', 'TranslateTest.CoreV2AssociativeUnorderedSetRenamedContainerStorageRejected', 'TranslateTest.CoreV2AssociativeUnorderedSetRuntimeStorageGuardRejected', 'TranslateTest.CoreV2AssociativeUnorderedMapNonDefaultKeyRejected', 'TranslateTest.CoreV2VariantMonostateValueForeignEmptyRejected', 'TranslateTest.CoreV2VariantMonostateValueRuntimeRedeclarationRejected', 'TranslateTest.CoreV2VariantIndexConstructionWrittenTagObjectRejected', 'TranslateTest.CoreV2VariantIndexConstructionEscapingTagRejected', 'TranslateTest.CoreV2VariantTypeConstructionWrittenTagObjectRejected', 'TranslateTest.CoreV2VariantTypeConstructionEffectfulTagRejected'])
        self.assertEqual(proof['original_test_names'], 8097)
        self.assertEqual(proof['selected_count'], 54)
        self.assertTrue(proof['all_non_test_source_retained'])
        for name in (b'variant-monostate-', b'variant-index-construction-', b'any-layout-', b'associative-'):
            self.assertIn(name, generated)

    def test_layout_in_place_rejects_altered_fixture_source_and_suffix(self):
        for altered in (self.layout_in_place_registered + b' ', self.layout_in_place_registered[:-1]):
            with self.assertRaises(AssertionError):
                self.adapter.restore_original_gtests(altered, self.layout_in_place_suffix)
            with self.assertRaises(AssertionError):
                self.adapter.restore_layout_in_place_history(altered)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_in_place_registered,
                                                 self.layout_in_place_suffix + b' ')


    def test_layout_in_place_any_retains_previous_case_bodies(self):
        self.assertEqual(self.layout_in_place_any_original + self.layout_in_place_any_suffix,
                         self.layout_in_place_any_registered)
        self.assertEqual(self.adapter.restore_layout_in_place_any_history(self.layout_in_place_any_original),
                         self.layout_in_place_original)
        generated, proof = self.extractor.extract(self.layout_in_place_any_original, ['TranslateTest.CoreV2AnyEmptyDefaultLocalRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyReceiverEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyTemporaryRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyMemberLifetimeRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyArrayLifetimeRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyUnprovedCopyRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyPointerCastMutableRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyPointerCastConstRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyPointerCastNullAndEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyPointerCastPointerTypeRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyCopyMoveRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyCopyCopyEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyAssignmentCopyIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyAssignmentOperatorNotationOrderRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyAssignmentExplicitAssignmentOrderRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyAssignmentMoveAndSelfRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyAssignmentSwapSequencingRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmptyAlteredObserverRejected', 'TranslateTest.CoreV2AnyEmptyAlteredResetRejected', 'TranslateTest.CoreV2AnyEmptyPointerCastValueFormRejected', 'TranslateTest.CoreV2AnyEmptyPointerCastUserRedeclarationRejected', 'TranslateTest.CoreV2AnyEmptyAssignmentRenamedSwapRejected'])
        self.assertEqual(proof['original_test_names'], 8119)
        self.assertEqual(proof['selected_count'], 22)
        self.assertTrue(proof['all_non_test_source_retained'])
        for name in (b'any-empty-assignment-', b'any-empty-array-', b'any-empty-pointer-cast-'):
            self.assertIn(name, generated)

    def test_layout_in_place_any_rejects_edited_source_and_suffix(self):
        for changed in (self.layout_in_place_any_registered + b' ', self.layout_in_place_any_registered[:-1]):
            with self.assertRaises(AssertionError):
                self.adapter.restore_original_gtests(changed, self.layout_in_place_any_suffix)
            with self.assertRaises(AssertionError):
                self.adapter.restore_layout_in_place_any_history(changed)
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_in_place_any_registered,
                                                 self.layout_in_place_any_suffix + b' ')


    def test_layout_variant_operations_retains_prior_source(self):
        self.assertEqual(self.layout_variant_operations_original + self.layout_variant_operations_suffix,
                         self.layout_variant_operations_registered)
        self.assertEqual(self.adapter.restore_layout_variant_operations_history(self.layout_variant_operations_original),
                         self.layout_in_place_any_original)
        generated, proof = self.extractor.extract(self.layout_variant_operations_original, ['TranslateTest.CoreV2MonostateComparisonAllSixRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateComparisonOperandEffectsRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateComparisonExplicitFunctionRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEmplaceIndexAndReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEmplaceTypeAndRepeatedValueRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEmplacePointerZeroRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEmplaceEmptyAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEmplaceReceiverBeforeInputRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateComparisonSdkAddressRejected', 'TranslateTest.CoreV2VariantEmplaceWideningOperandRejected', 'TranslateTest.CoreV2VariantEmplaceVolatileOperandRejected', 'TranslateTest.CoreV2VariantEmplaceSourceOwnedRecordRejected'])
        self.assertEqual(proof['original_test_names'], 8131)
        self.assertEqual(proof['selected_count'], 12)
        self.assertTrue(proof['all_non_test_source_retained'])
        self.assertIn(b'monostate', generated)
        self.assertIn(b'.emplace<', generated)

    def test_layout_variant_operations_rejects_edited_source_and_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_operations_history(self.layout_variant_operations_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_operations_registered,
                                                 self.layout_variant_operations_suffix + b' ')


    def test_layout_adapter_retains_published_bodies(self):
        self.assertEqual(self.layout_adapter_original + self.layout_adapter_suffix, self.layout_adapter_registered)
        self.assertEqual(self.adapter.restore_layout_adapter_history(self.layout_adapter_original),
                         self.layout_variant_operations_original)
        output, evidence = self.extractor.extract(self.layout_adapter_original, ['TranslateTest.CoreV2AssociativeMultimapIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultimapAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultimapSizeofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultimapAlignofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultisetIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultisetAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultisetSizeofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultisetAlignofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedMultisetIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedMultisetAliasesRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedMultisetSizeofRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeUnorderedMultisetAlignofRunAtBothOptimizations', 'TranslateTest.CoreV2DequeAdapterConstructionStackCopyDequeRunAtBothOptimizations', 'TranslateTest.CoreV2DequeAdapterConstructionQueueCopyConstDequeRunAtBothOptimizations', 'TranslateTest.CoreV2DequeAdapterConstructionQueueMoveDequeReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2DequeAdapterConstructionStackMoveDequeOnceRunAtBothOptimizations', 'TranslateTest.CoreV2AssociativeMultimapCustomTypeOrPolicyRejected', 'TranslateTest.CoreV2AssociativeMultimapRenamedContainerStorageRejected', 'TranslateTest.CoreV2AssociativeMultimapRuntimeStorageGuardRejected', 'TranslateTest.CoreV2AssociativeMultisetCustomTypeOrPolicyRejected', 'TranslateTest.CoreV2AssociativeMultisetRenamedContainerStorageRejected', 'TranslateTest.CoreV2AssociativeMultisetRuntimeStorageGuardRejected', 'TranslateTest.CoreV2AssociativeUnorderedMultisetCustomTypeOrPolicyRejected', 'TranslateTest.CoreV2AssociativeUnorderedMultisetRenamedContainerStorageRejected', 'TranslateTest.CoreV2AssociativeUnorderedMultisetRuntimeStorageGuardRejected', 'TranslateTest.CoreV2DequeAdapterConstructionVectorBackedStackRejected', 'TranslateTest.CoreV2DequeAdapterConstructionNonIntDequeRejected'])
        self.assertEqual(evidence['original_test_names'], 8158)
        self.assertEqual(evidence['selected_count'], 27)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'associative-unordered_multiset-', output)
        self.assertIn(b'deque-adapter-construction-stack-copy-deque', output)

    def test_layout_adapter_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_adapter_history(self.layout_adapter_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_adapter_registered, self.layout_adapter_suffix + b' ')


    def test_layout_assignment_visit_retains_published_bodies(self):
        self.assertEqual(self.layout_assignment_visit_original + self.layout_assignment_visit_suffix, self.layout_assignment_visit_registered)
        self.assertEqual(self.adapter.restore_layout_assignment_visit_history(self.layout_assignment_visit_original),
                         self.layout_adapter_original)
        output, evidence = self.extractor.extract(self.layout_assignment_visit_original, ['TranslateTest.CoreV2VariantValueAssignmentSwitchAndChainedResultRunAtBothOptimizations', 'TranslateTest.CoreV2VariantValueAssignmentSameAlternativeReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantValueAssignmentAssignmentSyntaxBindingOrderRunAtBothOptimizations', 'TranslateTest.CoreV2VariantValueAssignmentExplicitMemberBindingOrderRunAtBothOptimizations', 'TranslateTest.CoreV2VariantValueAssignmentPointerAndResetSelectionRunAtBothOptimizations', 'TranslateTest.CoreV2VariantZeroVisitFunctionRunAtBothOptimizations', 'TranslateTest.CoreV2VariantZeroVisitPointerReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantZeroVisitFactoryEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2VariantZeroVisitVoidResultRunAtBothOptimizations', 'TranslateTest.CoreV2VariantValueAssignmentVolatileOperandRejected', 'TranslateTest.CoreV2VariantValueAssignmentUserDefinedConversionRejected', 'TranslateTest.CoreV2VariantValueAssignmentNontrivialAlternativeRejected', 'TranslateTest.CoreV2VariantZeroVisitRenamedValueWrapperRejected', 'TranslateTest.CoreV2VariantZeroVisitRenamedTableProjectionRejected'])
        self.assertEqual(evidence['original_test_names'], 8172)
        self.assertEqual(evidence['selected_count'], 14)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-value-assignment-assignment-syntax-binding-order', output)
        self.assertIn(b'variant-zero-visit-void-result', output)

    def test_layout_assignment_visit_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_assignment_visit_history(self.layout_assignment_visit_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_assignment_visit_registered, self.layout_assignment_visit_suffix + b' ')


    def test_layout_integral_visit_retains_published_bodies(self):
        self.assertEqual(self.layout_integral_visit_original + self.layout_integral_visit_suffix, self.layout_integral_visit_registered)
        self.assertEqual(self.adapter.restore_layout_integral_visit_history(self.layout_integral_visit_original),
                         self.layout_assignment_visit_original)
        output, evidence = self.extractor.extract(self.layout_integral_visit_original, ['TranslateTest.CoreV2VariantIntegralVisitActiveIndexChangesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIntegralVisitConstAndIntegralConversionRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIntegralVisitCallbackAndReceiverEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIntegralVisitVoidResultRunAtBothOptimizations', 'TranslateTest.CoreV2VariantIntegralVisitRenamedGuardRejected', 'TranslateTest.CoreV2VariantIntegralVisitRenamedBaseSelectionRejected', 'TranslateTest.CoreV2VariantIntegralVisitGenericVisitorRejected'])
        self.assertEqual(evidence['original_test_names'], 8179)
        self.assertEqual(evidence['selected_count'], 7)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-integral-visit-active-index-changes', output)
        self.assertIn(b'variant-integral-visit-void-result', output)

    def test_layout_integral_visit_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_integral_visit_history(self.layout_integral_visit_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_integral_visit_registered, self.layout_integral_visit_suffix + b' ')


    def test_layout_any_integer_retains_published_bodies(self):
        self.assertEqual(self.layout_any_integer_original + self.layout_any_integer_suffix, self.layout_any_integer_registered)
        self.assertEqual(self.adapter.restore_layout_any_integer_history(self.layout_any_integer_original),
                         self.layout_integral_visit_original)
        output, evidence = self.extractor.extract(self.layout_any_integer_original, ['TranslateTest.CoreV2AnyIntRuntimeReferenceAndMismatchRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntCopyValueFromLvalueRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntCopyAndMoveConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntTransferSwapAndResetRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntUnsupportedFloatingValueRejected', 'TranslateTest.CoreV2AnyIntRenamedPinnedStorageHandlerRejected'])
        self.assertEqual(evidence['original_test_names'], 8185)
        self.assertEqual(evidence['selected_count'], 6)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-int-runtime-reference-and-mismatch', output)
        self.assertIn(b'any-int-transfer-swap-and-reset', output)

        promoted, _ = self.extractor.extract(self.layout_any_integer_original,
            ['TranslateTest.CoreV2AnyLayoutRuntimeConstructionRunAtBothOptimizations'])
        self.assertIn(b'std::any value{7};', promoted)
        self.assertIn(b'"-O0", "-O2"', promoted)
        self.assertNotIn(b'CoreV2AnyLayoutRuntimeConstructionRejected',
                         self.layout_any_integer_original)

    def test_layout_any_integer_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_any_integer_history(self.layout_any_integer_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_any_integer_registered, self.layout_any_integer_suffix + b' ')


    def test_layout_two_variant_any_retains_published_bodies(self):
        self.assertEqual(self.layout_two_variant_any_original + self.layout_two_variant_any_suffix, self.layout_two_variant_any_registered)
        self.assertEqual(self.adapter.restore_layout_two_variant_any_history(self.layout_two_variant_any_original),
                         self.layout_any_integer_original)
        output, evidence = self.extractor.extract(self.layout_two_variant_any_original, ['TranslateTest.CoreV2VariantTwoVisitMixedActiveIndicesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTwoVisitConstOperandAndMutationRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTwoVisitAliasAndEffectsWithVoidResultRunAtBothOptimizations', 'TranslateTest.CoreV2VariantTwoVisitRenamedMatrixIndexRejected', 'TranslateTest.CoreV2VariantTwoVisitRenamedMatrixDispatchRejected', 'TranslateTest.CoreV2VariantTwoVisitGenericVisitorRejected'])
        self.assertEqual(evidence['original_test_names'], 8191)
        self.assertEqual(evidence['selected_count'], 6)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-two-visit-mixed-active-indices', output)
        self.assertIn(b'variant-two-visit-const-operand-and-mutation', output)

    def test_layout_two_variant_any_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_two_variant_any_history(self.layout_two_variant_any_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_two_variant_any_registered, self.layout_two_variant_any_suffix + b' ')


    def test_layout_two_variant_any_tuple_retains_published_bodies(self):
        self.assertEqual(self.layout_two_variant_any_tuple_original + self.layout_two_variant_any_tuple_suffix, self.layout_two_variant_any_tuple_registered)
        self.assertEqual(self.adapter.restore_layout_two_variant_any_tuple_history(self.layout_two_variant_any_tuple_original),
                         self.layout_two_variant_any_original)
        output, evidence = self.extractor.extract(self.layout_two_variant_any_tuple_original, ['TranslateTest.CoreV2TupleBudgetDeep15ExecutionRunAtBothOptimizations', 'TranslateTest.CoreV2TupleBudgetDeep16ExecutionRunAtBothOptimizations'])
        self.assertEqual(evidence['original_test_names'], 8193)
        self.assertEqual(evidence['selected_count'], 2)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'tuple-budget-deep-15-execution', output)
        self.assertIn(b'tuple-budget-deep-16-execution', output)

    def test_layout_two_variant_any_tuple_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_two_variant_any_tuple_history(self.layout_two_variant_any_tuple_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_two_variant_any_tuple_registered, self.layout_two_variant_any_tuple_suffix + b' ')


    def test_layout_variant_any_tuple_visitor_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_any_tuple_visitor_original + self.layout_variant_any_tuple_visitor_suffix, self.layout_variant_any_tuple_visitor_registered)
        self.assertEqual(self.adapter.restore_layout_variant_any_tuple_visitor_history(self.layout_variant_any_tuple_visitor_original),
                         self.layout_two_variant_any_tuple_original)
        output, evidence = self.extractor.extract(self.layout_variant_any_tuple_visitor_original, ['TranslateTest.CoreV2AnyInPlaceValueInitRunAtBothOptimizations', 'TranslateTest.CoreV2AnyInPlaceEvaluatesArgumentOnceRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmplaceIntegerReplacesAndReturnsReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2AnyEmplaceIntegerEvaluatesArrowBeforeArgumentRunAtBothOptimizations', 'TranslateTest.CoreV2VariantOwnedVisitConstBorrowRunAtBothOptimizations', 'TranslateTest.CoreV2VariantOwnedVisitOverloadByActiveAlternativeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantOwnedVisitVoidBorrowEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2AnyInPlaceOtherPayloadGuardRejected', 'TranslateTest.CoreV2AnyEmplaceOtherPayloadGuardRejected', 'TranslateTest.CoreV2AnyInPlaceRenamedHandlerInstallationGuardRejected', 'TranslateTest.CoreV2VariantOwnedVisitRenamedInvokeRejected', 'TranslateTest.CoreV2VariantOwnedVisitRenamedDispatchRejected', 'TranslateTest.CoreV2VariantOwnedVisitRenamedIndexRejected'])
        self.assertEqual(evidence['original_test_names'], 8206)
        self.assertEqual(evidence['selected_count'], 13)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-in-place-value-init', output)
        self.assertIn(b'variant-owned-visit-void-borrow-evaluated-once', output)

    def test_layout_variant_any_tuple_visitor_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_any_tuple_visitor_history(self.layout_variant_any_tuple_visitor_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_any_tuple_visitor_registered, self.layout_variant_any_tuple_visitor_suffix + b' ')


    def test_layout_variant_any_allocator_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_any_allocator_original + self.layout_variant_any_allocator_suffix, self.layout_variant_any_allocator_registered)
        self.assertEqual(self.adapter.restore_layout_variant_any_allocator_history(self.layout_variant_any_allocator_original),
                         self.layout_variant_any_tuple_visitor_original)
        output, evidence = self.extractor.extract(self.layout_variant_any_allocator_original, ['TranslateTest.CoreV2AnyIntAssignAndReturnReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntAssignReadsReferenceAfterReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntMakeZeroInDestinationRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntMakeAtReturnBoundaryRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntConstValueAndBorrowedReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntConstValueFromInPlaceConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntMakeAndAssignWithCopyRunAtBothOptimizations', 'TranslateTest.CoreV2ScopedAllocatorSameTypeCopyRunAtBothOptimizations', 'TranslateTest.CoreV2ScopedAllocatorSameTypeMoveRunAtBothOptimizations', 'TranslateTest.CoreV2ScopedAllocatorCopyAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2VariantThreeVisitFunctionAndPointerRunAtBothOptimizations', 'TranslateTest.CoreV2VariantThreeVisitConstOverloadedVisitorRunAtBothOptimizations', 'TranslateTest.CoreV2VariantThreeVisitAliasedOperandsAndVoidRunAtBothOptimizations', 'TranslateTest.CoreV2AnyIntAssignOtherPayloadGuardRejected', 'TranslateTest.CoreV2AnyIntMakeRenamedInstallationGuardRejected', 'TranslateTest.CoreV2AnyIntCastWithoutActiveStateEvidenceRejected', 'TranslateTest.CoreV2ScopedAllocatorRenamedStorageCopyGuardRejected', 'TranslateTest.CoreV2ScopedAllocatorInnerPackCopyGuardRejected', 'TranslateTest.CoreV2VariantThreeVisitRenamedValuelessGuardRejected', 'TranslateTest.CoreV2VariantThreeVisitTemporaryOperandRejected', 'TranslateTest.CoreV2VariantThreeVisitMatrixCapacityRejected', 'TranslateTest.CoreV2VariantThreeVisitFourOperandsRejected'])
        self.assertEqual(evidence['original_test_names'], 8228)
        self.assertEqual(evidence['selected_count'], 22)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-int-make-and-assign-with-copy', output)
        self.assertIn(b'variant-three-visit-aliased-operands-and-void', output)

    def test_layout_variant_any_allocator_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_any_allocator_history(self.layout_variant_any_allocator_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_any_allocator_registered, self.layout_variant_any_allocator_suffix + b' ')


    def test_layout_variant_any_allocator_equality_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_any_allocator_equality_original + self.layout_variant_any_allocator_equality_suffix, self.layout_variant_any_allocator_equality_registered)
        self.assertEqual(self.adapter.restore_layout_variant_any_allocator_equality_history(self.layout_variant_any_allocator_equality_original),
                         self.layout_variant_any_allocator_original)
        output, evidence = self.extractor.extract(self.layout_variant_any_allocator_equality_original, ['TranslateTest.CoreV2VariantEqualityMatchingAndDifferentLiveAlternativesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEqualityDuplicateTypesRetainIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEqualityAliasedOperandsEvaluateOnceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantEqualityRenamedDiagonalVisitorRejected', 'TranslateTest.CoreV2VariantEqualityRenamedConversionPolicyRejected', 'TranslateTest.CoreV2VariantEqualityFloatingAlternativeRejected'])
        self.assertEqual(evidence['original_test_names'], 8234)
        self.assertEqual(evidence['selected_count'], 6)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-equality-matching-and-different-live-alternatives', output)
        self.assertIn(b'variant-equality-aliased-operands-evaluate-once', output)

    def test_layout_variant_any_allocator_equality_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_any_allocator_equality_history(self.layout_variant_any_allocator_equality_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_any_allocator_equality_registered, self.layout_variant_any_allocator_equality_suffix + b' ')


    def test_layout_variant_ordering_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_ordering_original + self.layout_variant_ordering_suffix, self.layout_variant_ordering_registered)
        self.assertEqual(self.adapter.restore_layout_variant_ordering_history(self.layout_variant_ordering_original),
                         self.layout_variant_any_allocator_equality_original)
        output, evidence = self.extractor.extract(self.layout_variant_ordering_original, ['TranslateTest.CoreV2VariantOrderingIndexBeforeValueRunAtBothOptimizations', 'TranslateTest.CoreV2VariantOrderingSignedValuesAndDuplicateTypesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantOrderingOnceOnlyConstOperandsRunAtBothOptimizations', 'TranslateTest.CoreV2VariantOrderingRenamedPolicyRejected', 'TranslateTest.CoreV2VariantOrderingTemporaryOperandRejected', 'TranslateTest.CoreV2VariantOrderingFloatingAlternativeRejected'])
        self.assertEqual(evidence['original_test_names'], 8240)
        self.assertEqual(evidence['selected_count'], 6)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-ordering-index-before-value', output)
        self.assertIn(b'variant-ordering-once-only-const-operands', output)

    def test_layout_variant_ordering_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_ordering_history(self.layout_variant_ordering_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_ordering_registered, self.layout_variant_ordering_suffix + b' ')


    def test_layout_variant_monostate_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_monostate_original + self.layout_variant_monostate_suffix, self.layout_variant_monostate_registered)
        self.assertEqual(self.adapter.restore_layout_variant_monostate_history(self.layout_variant_monostate_original),
                         self.layout_variant_ordering_original)
        output, evidence = self.extractor.extract(self.layout_variant_monostate_original, ['TranslateTest.CoreV2VariantMonostateComparisonSameIndexHasNoPayloadOrderRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateComparisonDistinctEmptyAlternativeIndexesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateComparisonReplacementEmptyClassRejected', 'TranslateTest.CoreV2VariantMonostateComparisonRenamedSdkMonostateRejected'])
        self.assertEqual(evidence['original_test_names'], 8244)
        self.assertEqual(evidence['selected_count'], 4)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-monostate-comparison-same-index-has-no-payload-order', output)
        self.assertIn(b'variant-monostate-comparison-distinct-empty-alternative-indexes', output)

    def test_layout_variant_monostate_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_monostate_history(self.layout_variant_monostate_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_monostate_registered, self.layout_variant_monostate_suffix + b' ')


    def test_layout_variant_monostate_hash_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_monostate_hash_original + self.layout_variant_monostate_hash_suffix, self.layout_variant_monostate_hash_registered)
        self.assertEqual(self.adapter.restore_layout_variant_monostate_hash_history(self.layout_variant_monostate_hash_original),
                         self.layout_variant_monostate_original)
        output, evidence = self.extractor.extract(self.layout_variant_monostate_hash_original, ['TranslateTest.CoreV2VariantMonostateHashTemporaryRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateHashReceiverAndOperandOnceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantMonostateHashAlternateSdkSpellingRejected', 'TranslateTest.CoreV2VariantMonostateHashStandaloneMemberPointerRejected'])
        self.assertEqual(evidence['original_test_names'], 8248)
        self.assertEqual(evidence['selected_count'], 4)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-monostate-hash-temporary', output)
        self.assertIn(b'variant-monostate-hash-receiver-and-operand-once', output)

    def test_layout_variant_monostate_hash_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_monostate_hash_history(self.layout_variant_monostate_hash_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_monostate_hash_registered, self.layout_variant_monostate_hash_suffix + b' ')


    def test_layout_variant_visit_alternatives_retains_published_bodies(self):
        self.assertEqual(self.layout_variant_visit_alternatives_original + self.layout_variant_visit_alternatives_suffix, self.layout_variant_visit_alternatives_registered)
        self.assertEqual(self.adapter.restore_layout_variant_visit_alternatives_history(self.layout_variant_visit_alternatives_original),
                         self.layout_variant_monostate_hash_original)
        output, evidence = self.extractor.extract(self.layout_variant_visit_alternatives_original, ['TranslateTest.CoreV2VariantVisitAlternativesFunctionReferenceAliasRunAtBothOptimizations', 'TranslateTest.CoreV2VariantVisitAlternativesMixedConstAndMutableBindingsRunAtBothOptimizations', 'TranslateTest.CoreV2VariantVisitAlternativesMonostateValueCopyRunAtBothOptimizations', 'TranslateTest.CoreV2VariantVisitAlternativesExactPointerAndFloatingAlternativesRunAtBothOptimizations', 'TranslateTest.CoreV2VariantVisitAlternativesRenamedExhaustiveCheckRejected', 'TranslateTest.CoreV2VariantVisitAlternativesReferenceToConvertedTemporaryRejected', 'TranslateTest.CoreV2VariantVisitAlternativesVolatileReferenceRejected', 'TranslateTest.CoreV2VariantVisitAlternativesReferenceResultRejected'])
        self.assertEqual(evidence['original_test_names'], 8256)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-visit-alternatives-function-reference-alias', output)
        self.assertIn(b'variant-visit-alternatives-mixed-const-and-mutable-bindings', output)

    def test_layout_variant_visit_alternatives_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_layout_variant_visit_alternatives_history(self.layout_variant_visit_alternatives_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.layout_variant_visit_alternatives_registered, self.layout_variant_visit_alternatives_suffix + b' ')


    def test_ordered_observer_query_retains_published_bodies(self):
        self.assertEqual(self.ordered_observer_query_original + self.ordered_observer_query_suffix, self.ordered_observer_query_registered)
        self.assertEqual(self.adapter.restore_ordered_observer_query_history(self.ordered_observer_query_original),
                         self.layout_variant_visit_alternatives_original)
        output, evidence = self.extractor.extract(self.ordered_observer_query_original, ['TranslateTest.CoreV2OrderedObserverQueryMapObserverSignaturesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedObserverQuerySetObserverSignaturesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedObserverQueryMultimapObserverSignaturesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedObserverQueryMultisetObserverSignaturesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedObserverQueryOverriddenKeyTypeRejected', 'TranslateTest.CoreV2OrderedObserverQueryRenamedSdkMemberRejected', 'TranslateTest.CoreV2OrderedObserverQueryObserverDoesNotAdmitStorageRejected'])
        self.assertEqual(evidence['original_test_names'], 8263)
        self.assertEqual(evidence['selected_count'], 7)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-observer-query-map-observer-signatures', output)
        self.assertIn(b'ordered-observer-query-multiset-observer-signatures', output)

    def test_ordered_observer_query_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_observer_query_history(self.ordered_observer_query_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_observer_query_registered, self.ordered_observer_query_suffix + b' ')


    def test_monostate_adapter_hash_retains_published_bodies(self):
        self.assertEqual(self.monostate_adapter_hash_original + self.monostate_adapter_hash_suffix, self.monostate_adapter_hash_registered)
        self.assertEqual(self.adapter.restore_monostate_adapter_hash_history(self.monostate_adapter_hash_original),
                         self.ordered_observer_query_original)
        output, evidence = self.extractor.extract(self.monostate_adapter_hash_original, ['TranslateTest.CoreV2MonostateAdapterHashDirectInvokeRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateAdapterHashTupleOfReferencesRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateAdapterHashReferenceWrapperCallRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateAdapterHashWrappedHasherThroughInvokeRunAtBothOptimizations', 'TranslateTest.CoreV2MonostateAdapterHashSourceConversionRejected', 'TranslateTest.CoreV2MonostateAdapterHashRenamedSdkMonostateRejected', 'TranslateTest.CoreV2MonostateAdapterHashMemberAddressThroughInvokeRejected'])
        self.assertEqual(evidence['original_test_names'], 8270)
        self.assertEqual(evidence['selected_count'], 7)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'monostate-adapter-hash-direct-invoke', output)
        self.assertIn(b'monostate-adapter-hash-tuple-of-references', output)

    def test_monostate_adapter_hash_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_monostate_adapter_hash_history(self.monostate_adapter_hash_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.monostate_adapter_hash_registered, self.monostate_adapter_hash_suffix + b' ')


    def test_variant_guarded_get_retains_published_bodies(self):
        self.assertEqual(self.variant_guarded_get_original + self.variant_guarded_get_suffix, self.variant_guarded_get_registered)
        self.assertEqual(self.adapter.restore_variant_guarded_get_history(self.variant_guarded_get_original),
                         self.monostate_adapter_hash_original)
        output, evidence = self.extractor.extract(self.variant_guarded_get_original, ['TranslateTest.CoreV2VariantGuardedGetMutableReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGuardedGetConstReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGuardedGetShortCircuitRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGuardedGetRecheckAfterEmplaceRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGuardedGetGuardForOtherObjectRejected', 'TranslateTest.CoreV2VariantGuardedGetGuardForOtherAlternativeRejected', 'TranslateTest.CoreV2VariantGuardedGetChangedAfterGuardRejected', 'TranslateTest.CoreV2VariantGuardedGetFalsePathRejected'])
        self.assertEqual(evidence['original_test_names'], 8278)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-guarded-get-mutable-reference', output)
        self.assertIn(b'variant-guarded-get-const-reference', output)

    def test_variant_guarded_get_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_variant_guarded_get_history(self.variant_guarded_get_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.variant_guarded_get_registered, self.variant_guarded_get_suffix + b' ')


    def test_any_unsigned_inline_retains_published_bodies(self):
        self.assertEqual(self.any_unsigned_inline_original + self.any_unsigned_inline_suffix, self.any_unsigned_inline_registered)
        self.assertEqual(self.adapter.restore_any_unsigned_inline_history(self.any_unsigned_inline_original),
                         self.variant_guarded_get_original)
        output, evidence = self.extractor.extract(self.any_unsigned_inline_original, ['TranslateTest.CoreV2AnyUnsignedInlineConstructorAndTypedPointerRunAtBothOptimizations', 'TranslateTest.CoreV2AnyUnsignedInlineInPlaceAndConstValueRunAtBothOptimizations', 'TranslateTest.CoreV2AnyUnsignedInlineFactoryCopyMoveAndResetRunAtBothOptimizations', 'TranslateTest.CoreV2AnyUnsignedInlineChangeIntegerKindRunAtBothOptimizations', 'TranslateTest.CoreV2AnyUnsignedInlineUnprovedSignedValueCastRejected', 'TranslateTest.CoreV2AnyUnsignedInlineUnprovedUnsignedValueCastRejected', 'TranslateTest.CoreV2AnyUnsignedInlineRenamedUnsignedHandlerRejected', 'TranslateTest.CoreV2AnyUnsignedInlineUnsignedLongIsNotUnsignedIntRejected'])
        self.assertEqual(evidence['original_test_names'], 8286)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-unsigned-inline-constructor-and-typed-pointer', output)
        self.assertIn(b'any-unsigned-inline-change-integer-kind', output)

    def test_any_unsigned_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_unsigned_inline_history(self.any_unsigned_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_unsigned_inline_registered, self.any_unsigned_inline_suffix + b' ')


    def test_variant_get_early_return_retains_published_bodies(self):
        self.assertEqual(self.variant_get_early_return_original + self.variant_get_early_return_suffix, self.variant_get_early_return_registered)
        self.assertEqual(self.adapter.restore_variant_get_early_return_history(self.variant_get_early_return_original),
                         self.any_unsigned_inline_original)
        output, evidence = self.extractor.extract(self.variant_get_early_return_original, ['TranslateTest.CoreV2VariantGetEarlyReturnReturnsOnDifferentTypeRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGetEarlyReturnReturnsOnDifferentIndexRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGetEarlyReturnCheckedElseBranchRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGetEarlyReturnScopeLocalAfterReturnRunAtBothOptimizations', 'TranslateTest.CoreV2VariantGetEarlyReturnFailingBranchFallsThroughRejected', 'TranslateTest.CoreV2VariantGetEarlyReturnDestroyedIfInitializerRejected', 'TranslateTest.CoreV2VariantGetEarlyReturnInterveningMutationRejected', 'TranslateTest.CoreV2VariantGetEarlyReturnReturnGuardOnOtherObjectRejected'])
        self.assertEqual(evidence['original_test_names'], 8294)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'variant-get-early-return-returns-on-different-type', output)
        self.assertIn(b'variant-get-early-return-checked-else-branch', output)

    def test_variant_get_early_return_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_variant_get_early_return_history(self.variant_get_early_return_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.variant_get_early_return_registered, self.variant_get_early_return_suffix + b' ')


    def test_ordered_empty_owner_retains_published_bodies(self):
        self.assertEqual(self.ordered_empty_owner_original + self.ordered_empty_owner_suffix, self.ordered_empty_owner_registered)
        self.assertEqual(self.adapter.restore_ordered_empty_owner_history(self.ordered_empty_owner_original),
                         self.variant_get_early_return_original)
        output, evidence = self.extractor.extract(self.ordered_empty_owner_original, ['TranslateTest.CoreV2OrderedEmptyOwnerMapObserversRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedEmptyOwnerConstMultimapRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedEmptyOwnerSetReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedEmptyOwnerMultisetScopedOwnersRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedEmptyOwnerCustomComparatorRejected', 'TranslateTest.CoreV2OrderedEmptyOwnerDifferentKeyRejected', 'TranslateTest.CoreV2OrderedEmptyOwnerSetInsertRejected', 'TranslateTest.CoreV2OrderedEmptyOwnerMapCopyRejected'])
        self.assertEqual(evidence['original_test_names'], 8302)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-empty-owner-map-observers', output)
        self.assertIn(b'ordered-empty-owner-multiset-scoped-owners', output)

        promoted, _ = self.extractor.extract(self.ordered_empty_owner_original,
            ['TranslateTest.CoreV2OrderedEmptyOwnerOriginalMapRunsAtBothOptimizations'])
        self.assertIn(b'return values.empty() ? 0 : 1;', promoted)
        self.assertIn(b'"-O0", "-O2"', promoted)
        self.assertNotIn(b'CoreV2OrderedObserverQueryObserverDoesNotAdmitStorageRejected',
                         self.ordered_empty_owner_original)

    def test_ordered_empty_owner_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_empty_owner_history(self.ordered_empty_owner_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_empty_owner_registered, self.ordered_empty_owner_suffix + b' ')


    def test_ordered_max_size_retains_published_bodies(self):
        self.assertEqual(self.ordered_max_size_original + self.ordered_max_size_suffix, self.ordered_max_size_registered)
        self.assertEqual(self.adapter.restore_ordered_max_size_history(self.ordered_max_size_original),
                         self.ordered_empty_owner_original)
        output, evidence = self.extractor.extract(self.ordered_max_size_original, ['TranslateTest.CoreV2OrderedMaxSizeMapRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMaxSizeConstMultimapRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMaxSizeSetReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMaxSizeMultisetRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMaxSizeDifferentElementRejected', 'TranslateTest.CoreV2OrderedMaxSizeMutableTreesRejected', 'TranslateTest.CoreV2OrderedMaxSizeCustomComparatorRejected', 'TranslateTest.CoreV2OrderedMaxSizeTemporaryRejected'])
        self.assertEqual(evidence['original_test_names'], 8310)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-max-size-const-multimap', output)
        self.assertIn(b'ordered-max-size-set-receiver-once', output)

    def test_ordered_max_size_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_max_size_history(self.ordered_max_size_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_max_size_registered, self.ordered_max_size_suffix + b' ')


    def test_any_bool_inline_retains_published_bodies(self):
        self.assertEqual(self.any_bool_inline_original + self.any_bool_inline_suffix, self.any_bool_inline_registered)
        self.assertEqual(self.adapter.restore_any_bool_inline_history(self.any_bool_inline_original),
                         self.ordered_max_size_original)
        output, evidence = self.extractor.extract(self.any_bool_inline_original, ['TranslateTest.CoreV2AnyBoolInlineTypedMutablePointerRunAtBothOptimizations', 'TranslateTest.CoreV2AnyBoolInlineFactoryEmplaceResetRunAtBothOptimizations', 'TranslateTest.CoreV2AnyBoolInlineCopyMoveAndAssignmentRunAtBothOptimizations', 'TranslateTest.CoreV2AnyBoolInlineConstantActiveValueRunAtBothOptimizations', 'TranslateTest.CoreV2AnyBoolInlineValueMismatchRejected', 'TranslateTest.CoreV2AnyBoolInlineDynamicValueTagRejected', 'TranslateTest.CoreV2AnyBoolInlinePointerPayloadRejected', 'TranslateTest.CoreV2AnyBoolInlineMutableReferenceCastRejected'])
        self.assertEqual(evidence['original_test_names'], 8318)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-bool-inline-constant-active-value', output)
        self.assertIn(b'any-bool-inline-typed-mutable-pointer', output)

    def test_any_bool_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_bool_inline_history(self.any_bool_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_bool_inline_registered, self.any_bool_inline_suffix + b' ')


    def test_empty_hash_set_retains_published_bodies(self):
        self.assertEqual(self.empty_hash_set_original + self.empty_hash_set_suffix, self.empty_hash_set_registered)
        self.assertEqual(self.adapter.restore_empty_hash_set_history(self.empty_hash_set_original),
                         self.any_bool_inline_original)
        output, evidence = self.extractor.extract(self.empty_hash_set_original, ['TranslateTest.CoreV2EmptyHashSetSetSizeRunAtBothOptimizations', 'TranslateTest.CoreV2EmptyHashSetConstMultisetRunAtBothOptimizations', 'TranslateTest.CoreV2EmptyHashSetReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2EmptyHashSetScopedMultisetRunAtBothOptimizations', 'TranslateTest.CoreV2EmptyHashSetCustomHashRejected', 'TranslateTest.CoreV2EmptyHashSetInsertRejected', 'TranslateTest.CoreV2EmptyHashSetCopyRejected', 'TranslateTest.CoreV2EmptyHashSetLoadFactorSetterRejected'])
        self.assertEqual(evidence['original_test_names'], 8326)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'empty-hash-set-set-size', output)
        self.assertIn(b'empty-hash-set-receiver-once', output)

    def test_empty_hash_set_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_empty_hash_set_history(self.empty_hash_set_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.empty_hash_set_registered, self.empty_hash_set_suffix + b' ')


    def test_any_float_inline_retains_published_bodies(self):
        self.assertEqual(self.any_float_inline_original + self.any_float_inline_suffix, self.any_float_inline_registered)
        self.assertEqual(self.adapter.restore_any_float_inline_history(self.any_float_inline_original),
                         self.empty_hash_set_original)
        output, evidence = self.extractor.extract(self.any_float_inline_original, ['TranslateTest.CoreV2AnyFloatInlinePointerWriteRunAtBothOptimizations', 'TranslateTest.CoreV2AnyFloatInlineImmutableValueRunAtBothOptimizations', 'TranslateTest.CoreV2AnyFloatInlineFactoryEmplaceResetRunAtBothOptimizations', 'TranslateTest.CoreV2AnyFloatInlineCopyMoveAndAssignRunAtBothOptimizations', 'TranslateTest.CoreV2AnyFloatInlineLongDoubleOwnerRejected', 'TranslateTest.CoreV2AnyFloatInlineMutableReferenceRejected', 'TranslateTest.CoreV2AnyFloatInlineUnknownActiveValueRejected', 'TranslateTest.CoreV2AnyFloatInlineMismatchedImmutableValueRejected'])
        self.assertEqual(evidence['original_test_names'], 8334)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-float-inline-immutable-value', output)
        self.assertIn(b'any-float-inline-pointer-write', output)

    def test_any_float_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_float_inline_history(self.any_float_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_float_inline_registered, self.any_float_inline_suffix + b' ')


    def test_unordered_empty_load_factor_retains_published_bodies(self):
        self.assertEqual(self.unordered_empty_load_factor_original + self.unordered_empty_load_factor_suffix, self.unordered_empty_load_factor_registered)
        self.assertEqual(self.adapter.restore_unordered_empty_load_factor_history(self.unordered_empty_load_factor_original),
                         self.any_float_inline_original)
        output, evidence = self.extractor.extract(self.unordered_empty_load_factor_original, ['TranslateTest.CoreV2UnorderedEmptyLoadFactorEmptySetRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorEmptyMultisetRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorSingleReceiverEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorNestedScopeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorCustomHashRejected', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorDifferentKeyRejected', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorRehashRejected', 'TranslateTest.CoreV2UnorderedEmptyLoadFactorPointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8342)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-empty-load-factor-empty-set', output)
        self.assertIn(b'unordered-empty-load-factor-single-receiver-evaluation', output)

    def test_unordered_empty_load_factor_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_empty_load_factor_history(self.unordered_empty_load_factor_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_empty_load_factor_registered, self.unordered_empty_load_factor_suffix + b' ')


    def test_unordered_empty_capacity_retains_published_bodies(self):
        self.assertEqual(self.unordered_empty_capacity_original + self.unordered_empty_capacity_suffix, self.unordered_empty_capacity_registered)
        self.assertEqual(self.adapter.restore_unordered_empty_capacity_history(self.unordered_empty_capacity_original),
                         self.unordered_empty_load_factor_original)
        output, evidence = self.extractor.extract(self.unordered_empty_capacity_original, ['TranslateTest.CoreV2UnorderedEmptyCapacitySetMaxSizeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyCapacityMultisetMaxBucketsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyCapacitySingleReceiverEvaluationRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyCapacityNestedMultisetRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedEmptyCapacityCustomHashRejected', 'TranslateTest.CoreV2UnorderedEmptyCapacityNonIntKeyRejected', 'TranslateTest.CoreV2UnorderedEmptyCapacityArrowReceiverRejected', 'TranslateTest.CoreV2UnorderedEmptyCapacityNonemptyOwnerRejected'])
        self.assertEqual(evidence['original_test_names'], 8350)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-empty-capacity-set-max-size', output)
        self.assertIn(b'unordered-empty-capacity-single-receiver-evaluation', output)

    def test_unordered_empty_capacity_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_empty_capacity_history(self.unordered_empty_capacity_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_empty_capacity_registered, self.unordered_empty_capacity_suffix + b' ')


    def test_unordered_lookup_policies_retains_published_bodies(self):
        self.assertEqual(self.unordered_lookup_policies_original + self.unordered_lookup_policies_suffix, self.unordered_lookup_policies_registered)
        self.assertEqual(self.adapter.restore_unordered_lookup_policies_history(self.unordered_lookup_policies_original),
                         self.unordered_empty_capacity_original)
        output, evidence = self.extractor.extract(self.unordered_lookup_policies_original, ['TranslateTest.CoreV2UnorderedLookupPoliciesSetHashValueRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLookupPoliciesMultisetEqualityValueRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLookupPoliciesHashReceiverEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLookupPoliciesEquivalentCopiesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLookupPoliciesCustomHashRejected', 'TranslateTest.CoreV2UnorderedLookupPoliciesCustomEqualityRejected', 'TranslateTest.CoreV2UnorderedLookupPoliciesArrowReceiverRejected', 'TranslateTest.CoreV2UnorderedLookupPoliciesNonemptyOwnerRejected'])
        self.assertEqual(evidence['original_test_names'], 8358)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-lookup-policies-set-hash-value', output)
        self.assertIn(b'unordered-lookup-policies-hash-receiver-evaluated-once', output)

    def test_unordered_lookup_policies_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_lookup_policies_history(self.unordered_lookup_policies_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_lookup_policies_registered, self.unordered_lookup_policies_suffix + b' ')


    def test_unordered_load_limit_retains_published_bodies(self):
        self.assertEqual(self.unordered_load_limit_original + self.unordered_load_limit_suffix, self.unordered_load_limit_registered)
        self.assertEqual(self.adapter.restore_unordered_load_limit_history(self.unordered_load_limit_original),
                         self.unordered_lookup_policies_original)
        output, evidence = self.extractor.extract(self.unordered_load_limit_original, ['TranslateTest.CoreV2UnorderedLoadLimitSetUpdateRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLoadLimitMultisetUpdateRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLoadLimitReceiverBeforeArgumentRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLoadLimitIndependentEmptyOwnersRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedLoadLimitCustomHashRejected', 'TranslateTest.CoreV2UnorderedLoadLimitNonIntKeyRejected', 'TranslateTest.CoreV2UnorderedLoadLimitArrowReceiverRejected', 'TranslateTest.CoreV2UnorderedLoadLimitNonemptyOwnerRejected'])
        self.assertEqual(evidence['original_test_names'], 8366)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-load-limit-set-update', output)
        self.assertIn(b'unordered-load-limit-receiver-before-argument', output)

    def test_unordered_load_limit_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_load_limit_history(self.unordered_load_limit_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_load_limit_registered, self.unordered_load_limit_suffix + b' ')


    def test_any_wide_inline_retains_published_bodies(self):
        self.assertEqual(self.any_wide_inline_original + self.any_wide_inline_suffix, self.any_wide_inline_registered)
        self.assertEqual(self.adapter.restore_any_wide_inline_history(self.any_wide_inline_original),
                         self.unordered_load_limit_original)
        output, evidence = self.extractor.extract(self.any_wide_inline_original, ['TranslateTest.CoreV2AnyWideInlineDoublePointerPrecisionRunAtBothOptimizations', 'TranslateTest.CoreV2AnyWideInlineDoubleInPlaceDefaultRunAtBothOptimizations', 'TranslateTest.CoreV2AnyWideInlineWideSignedValueMoveRunAtBothOptimizations', 'TranslateTest.CoreV2AnyWideInlineWideUnsignedEmplaceRunAtBothOptimizations', 'TranslateTest.CoreV2AnyWideInlineWideTagsAndSwapRunAtBothOptimizations', 'TranslateTest.CoreV2AnyWideInlineWideInputOnceRunAtBothOptimizations', 'TranslateTest.CoreV2AnyWideInlineMutableWideReferenceRejected', 'TranslateTest.CoreV2AnyWideInlineUnknownWideValueRejected', 'TranslateTest.CoreV2AnyWideInlineMismatchedWideValueRejected', 'TranslateTest.CoreV2AnyWideInlineRecordConversionRejected'])
        self.assertEqual(evidence['original_test_names'], 8376)
        self.assertEqual(evidence['selected_count'], 10)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-wide-inline-double-pointer-precision', output)
        self.assertIn(b'any-wide-inline-wide-input-once', output)

    def test_any_wide_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_wide_inline_history(self.any_wide_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_wide_inline_registered, self.any_wide_inline_suffix + b' ')


    def test_ordered_comparator_retains_published_bodies(self):
        self.assertEqual(self.ordered_comparator_original + self.ordered_comparator_suffix, self.ordered_comparator_registered)
        self.assertEqual(self.adapter.restore_ordered_comparator_history(self.ordered_comparator_original),
                         self.any_wide_inline_original)
        output, evidence = self.extractor.extract(self.ordered_comparator_original, ['TranslateTest.CoreV2OrderedComparatorSetKeyAndValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedComparatorMultisetConstValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedComparatorMapKeyWrapperRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedComparatorMultimapKeyWrapperRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedComparatorCustomComparatorRejected', 'TranslateTest.CoreV2OrderedComparatorMapValueComparatorRejected', 'TranslateTest.CoreV2OrderedComparatorNonDefaultKeyRejected', 'TranslateTest.CoreV2OrderedComparatorArrowReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8384)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-comparator-set-key-and-value', output)
        self.assertIn(b'ordered-comparator-map-key-wrapper', output)

    def test_ordered_comparator_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_comparator_history(self.ordered_comparator_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_comparator_registered, self.ordered_comparator_suffix + b' ')


    def test_unordered_bucket_storage_retains_published_bodies(self):
        self.assertEqual(self.unordered_bucket_storage_original + self.unordered_bucket_storage_suffix, self.unordered_bucket_storage_registered)
        self.assertEqual(self.adapter.restore_unordered_bucket_storage_history(self.unordered_bucket_storage_original),
                         self.ordered_comparator_original)
        output, evidence = self.extractor.extract(self.unordered_bucket_storage_original, ['TranslateTest.CoreV2UnorderedBucketStorageUniqueRehashRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedBucketStorageMultiReserveLimitRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedBucketStorageResizeArgumentsOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedBucketStorageBucketAllocationAndReleaseRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedBucketStorageCustomHasherRejected', 'TranslateTest.CoreV2UnorderedBucketStorageDifferentKeyRejected', 'TranslateTest.CoreV2UnorderedBucketStoragePointerReceiverRejected', 'TranslateTest.CoreV2UnorderedBucketStorageNonemptyOwnerRejected'])
        self.assertEqual(evidence['original_test_names'], 8392)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-bucket-storage-unique-rehash', output)
        self.assertIn(b'unordered-bucket-storage-bucket-allocation-and-release', output)

    def test_unordered_bucket_storage_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_bucket_storage_history(self.unordered_bucket_storage_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_bucket_storage_registered, self.unordered_bucket_storage_suffix + b' ')


    def test_unordered_set_values_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_values_original + self.unordered_set_values_suffix, self.unordered_set_values_registered)
        self.assertEqual(self.adapter.restore_unordered_set_values_history(self.unordered_set_values_original),
                         self.unordered_bucket_storage_original)
        output, evidence = self.extractor.extract(self.unordered_set_values_original, ['TranslateTest.CoreV2UnorderedSetValuesBracesDuplicatesAndCountRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetValuesListInputOrderRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetValuesNonemptyRehashAndBucketsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetValuesClearRetainsBucketCapacityRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetValuesDifferentKeyListRejected', 'TranslateTest.CoreV2UnorderedSetValuesCustomKeyEqualityRejected', 'TranslateTest.CoreV2UnorderedSetValuesMultipleKeyOwnerRejected', 'TranslateTest.CoreV2UnorderedSetValuesArrowCountRejected'])
        self.assertEqual(evidence['original_test_names'], 8400)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-values-braces-duplicates-and-count', output)
        self.assertIn(b'unordered-set-values-clear-retains-bucket-capacity', output)

    def test_unordered_set_values_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_values_history(self.unordered_set_values_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_values_registered, self.unordered_set_values_suffix + b' ')


    def test_unordered_set_modifiers_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_modifiers_original + self.unordered_set_modifiers_suffix, self.unordered_set_modifiers_registered)
        self.assertEqual(self.adapter.restore_unordered_set_modifiers_history(self.unordered_set_modifiers_original),
                         self.unordered_set_values_original)
        output, evidence = self.extractor.extract(self.unordered_set_modifiers_original, ['TranslateTest.CoreV2UnorderedSetModifiersEraseCollisionsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetModifiersReceiverBeforeModifierArgumentsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetModifiersInsertAfterClearRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetModifiersEraseReleasesOneNodeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetModifiersDifferentKeyRejected', 'TranslateTest.CoreV2UnorderedSetModifiersAlternateHashRejected', 'TranslateTest.CoreV2UnorderedSetModifiersMultiKeyEraseRejected', 'TranslateTest.CoreV2UnorderedSetModifiersPointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8408)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-modifiers-erase-collisions', output)
        self.assertIn(b'unordered-set-modifiers-erase-releases-one-node', output)

    def test_unordered_set_modifiers_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_modifiers_history(self.unordered_set_modifiers_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_modifiers_registered, self.unordered_set_modifiers_suffix + b' ')


    def test_any_narrow_inline_retains_published_bodies(self):
        self.assertEqual(self.any_narrow_inline_original + self.any_narrow_inline_suffix, self.any_narrow_inline_registered)
        self.assertEqual(self.adapter.restore_any_narrow_inline_history(self.any_narrow_inline_original),
                         self.unordered_set_modifiers_original)
        output, evidence = self.extractor.extract(self.any_narrow_inline_original, ['TranslateTest.CoreV2AnyNarrowInlineDistinctCharTagsRunAtBothOptimizations', 'TranslateTest.CoreV2AnyNarrowInlineShortLifecycleRunAtBothOptimizations', 'TranslateTest.CoreV2AnyNarrowInlineSignedNarrowValueCastsRunAtBothOptimizations', 'TranslateTest.CoreV2AnyNarrowInlineUnsignedNarrowReferenceAndResetRunAtBothOptimizations', 'TranslateTest.CoreV2AnyNarrowInlineUnicodePayloadRejected', 'TranslateTest.CoreV2AnyNarrowInlineDifferentSmallRecordRejected', 'TranslateTest.CoreV2AnyNarrowInlineMutableNarrowValueCastRejected', 'TranslateTest.CoreV2AnyNarrowInlineNarrowTagMismatchValueCastRejected'])
        self.assertEqual(evidence['original_test_names'], 8416)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-narrow-inline-distinct-char-tags', output)
        self.assertIn(b'any-narrow-inline-short-lifecycle', output)

    def test_any_narrow_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_narrow_inline_history(self.any_narrow_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_narrow_inline_registered, self.any_narrow_inline_suffix + b' ')


    def test_any_long_inline_retains_published_bodies(self):
        self.assertEqual(self.any_long_inline_original + self.any_long_inline_suffix, self.any_long_inline_registered)
        self.assertEqual(self.adapter.restore_any_long_inline_history(self.any_long_inline_original),
                         self.any_narrow_inline_original)
        output, evidence = self.extractor.extract(self.any_long_inline_original, ['TranslateTest.CoreV2AnyLongInlineDistinctNativeLongTagsRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLongInlineMakeAndCopyNativeLongRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLongInlineLongEmplaceAndMoveRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLongInlineSwapNativeLongAndSignedWideRunAtBothOptimizations', 'TranslateTest.CoreV2AnyLongInlineSignedLongTagMismatchRejected', 'TranslateTest.CoreV2AnyLongInlineUnsignedLongTagMismatchRejected', 'TranslateTest.CoreV2AnyLongInlineMutableLongReferenceCastRejected', 'TranslateTest.CoreV2AnyLongInlineVolatileNativeLongAccessRejected'])
        self.assertEqual(evidence['original_test_names'], 8424)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-long-inline-distinct-native-long-tags', output)
        self.assertIn(b'any-long-inline-long-emplace-and-move', output)

    def test_any_long_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_long_inline_history(self.any_long_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_long_inline_registered, self.any_long_inline_suffix + b' ')


    def test_unordered_multiset_values_retains_published_bodies(self):
        self.assertEqual(self.unordered_multiset_values_original + self.unordered_multiset_values_suffix, self.unordered_multiset_values_registered)
        self.assertEqual(self.adapter.restore_unordered_multiset_values_history(self.unordered_multiset_values_original),
                         self.any_long_inline_original)
        output, evidence = self.extractor.extract(self.unordered_multiset_values_original, ['TranslateTest.CoreV2UnorderedMultisetValuesCollisionBucketsPreserveMultiplicityRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetValuesReservePreservesDuplicateKeysRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetValuesReleaseEachDuplicateNodeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetValuesConstCountEvaluatesReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetValuesDifferentKeyRejected', 'TranslateTest.CoreV2UnorderedMultisetValuesAlternateHashRejected', 'TranslateTest.CoreV2UnorderedMultisetValuesScalarInsertRejected', 'TranslateTest.CoreV2UnorderedMultisetValuesArrowCountRejected'])
        self.assertEqual(evidence['original_test_names'], 8432)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-multiset-values-collision-buckets-preserve-multiplicity', output)
        self.assertIn(b'unordered-multiset-values-release-each-duplicate-node', output)

    def test_unordered_multiset_values_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_multiset_values_history(self.unordered_multiset_values_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_multiset_values_registered, self.unordered_multiset_values_suffix + b' ')


    def test_any_character_inline_retains_published_bodies(self):
        self.assertEqual(self.any_character_inline_original + self.any_character_inline_suffix, self.any_character_inline_registered)
        self.assertEqual(self.adapter.restore_any_character_inline_history(self.any_character_inline_original),
                         self.unordered_multiset_values_original)
        output, evidence = self.extractor.extract(self.any_character_inline_original, ['TranslateTest.CoreV2AnyCharacterInlineWideTagDistinctFromIntegersRunAtBothOptimizations', 'TranslateTest.CoreV2AnyCharacterInlineUtfTagTransitionRunAtBothOptimizations', 'TranslateTest.CoreV2AnyCharacterInlineUtfPayloadCopyAndSwapRunAtBothOptimizations', 'TranslateTest.CoreV2AnyCharacterInlineClosedWideAndUtfValueCastsRunAtBothOptimizations', 'TranslateTest.CoreV2AnyCharacterInlineMutableUtfReferenceRejected', 'TranslateTest.CoreV2AnyCharacterInlineWrongUtfValueKindRejected', 'TranslateTest.CoreV2AnyCharacterInlineAggregateWithUtfMemberRejected', 'TranslateTest.CoreV2AnyCharacterInlineVolatileUtfPointerRejected'])
        self.assertEqual(evidence['original_test_names'], 8440)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'any-character-inline-utf-tag-transition', output)
        self.assertIn(b'any-character-inline-wide-tag-distinct-from-integers', output)

    def test_any_character_inline_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_any_character_inline_history(self.any_character_inline_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.any_character_inline_registered, self.any_character_inline_suffix + b' ')


    def test_unordered_multiset_modifiers_retains_published_bodies(self):
        self.assertEqual(self.unordered_multiset_modifiers_original + self.unordered_multiset_modifiers_suffix, self.unordered_multiset_modifiers_registered)
        self.assertEqual(self.adapter.restore_unordered_multiset_modifiers_history(self.unordered_multiset_modifiers_original),
                         self.any_character_inline_original)
        output, evidence = self.extractor.extract(self.unordered_multiset_modifiers_original, ['TranslateTest.CoreV2UnorderedMultisetModifiersInsertListRetainsEachDuplicateRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetModifiersEraseRepairsCollidingBucketPredecessorsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetModifiersRehashAfterErasingGroupsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetModifiersReceiverAndListSequencedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetModifiersEraseOneFreePerDuplicateRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultisetModifiersDifferentKeyListRejected', 'TranslateTest.CoreV2UnorderedMultisetModifiersCustomAllowsDifferentKeyEqualityRejected', 'TranslateTest.CoreV2UnorderedMultisetModifiersPointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8448)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-multiset-modifiers-insert-list-retains-each-duplicate', output)
        self.assertIn(b'unordered-multiset-modifiers-erase-one-free-per-duplicate', output)

    def test_unordered_multiset_modifiers_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_multiset_modifiers_history(self.unordered_multiset_modifiers_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_multiset_modifiers_registered, self.unordered_multiset_modifiers_suffix + b' ')


    def test_unordered_set_global_iterators_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_global_iterators_original + self.unordered_set_global_iterators_suffix, self.unordered_set_global_iterators_registered)
        self.assertEqual(self.adapter.restore_unordered_set_global_iterators_history(self.unordered_set_global_iterators_original),
                         self.unordered_multiset_modifiers_original)
        output, evidence = self.extractor.extract(self.unordered_set_global_iterators_original, ['TranslateTest.CoreV2UnorderedSetGlobalIteratorsForwardWalkRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsConstFindAndMissingRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsDuplicateTraversalRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsEndpointsEvaluateReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsLocalBucketIteratorRejected', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsDifferentKeyIteratorRejected', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsCustomHashFindRejected', 'TranslateTest.CoreV2UnorderedSetGlobalIteratorsFindPointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8456)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-global-iterators-forward-walk', output)
        self.assertIn(b'unordered-set-global-iterators-duplicate-traversal', output)

    def test_unordered_set_global_iterators_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_global_iterators_history(self.unordered_set_global_iterators_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_global_iterators_registered, self.unordered_set_global_iterators_suffix + b' ')


    def test_unordered_set_scalar_insertion_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_scalar_insertion_original + self.unordered_set_scalar_insertion_suffix, self.unordered_set_scalar_insertion_registered)
        self.assertEqual(self.adapter.restore_unordered_set_scalar_insertion_history(self.unordered_set_scalar_insertion_original),
                         self.unordered_set_global_iterators_original)
        output, evidence = self.extractor.extract(self.unordered_set_scalar_insertion_original, ['TranslateTest.CoreV2UnorderedSetScalarInsertionSetReturnsPositionAndInsertionFlagRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetScalarInsertionSetInsertionEvaluatesReceiverAndArgumentOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetScalarInsertionMultisetReturnsTheNewNodeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetScalarInsertionMixedScalarAndListInsertRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetScalarInsertionInsertWithHintRejected', 'TranslateTest.CoreV2UnorderedSetScalarInsertionCustomHashRejected', 'TranslateTest.CoreV2UnorderedSetScalarInsertionDifferentKeyTypeRejected', 'TranslateTest.CoreV2UnorderedSetScalarInsertionPointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8464)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-scalar-insertion-set-returns-position-and-insertion-flag', output)
        self.assertIn(b'unordered-set-scalar-insertion-multiset-returns-the-new-node', output)

    def test_unordered_set_scalar_insertion_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_scalar_insertion_history(self.unordered_set_scalar_insertion_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_scalar_insertion_registered, self.unordered_set_scalar_insertion_suffix + b' ')


    def test_unordered_set_iterator_erase_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_iterator_erase_original + self.unordered_set_iterator_erase_suffix, self.unordered_set_iterator_erase_registered)
        self.assertEqual(self.adapter.restore_unordered_set_iterator_erase_history(self.unordered_set_iterator_erase_original),
                         self.unordered_set_scalar_insertion_original)
        output, evidence = self.extractor.extract(self.unordered_set_iterator_erase_original, ['TranslateTest.CoreV2UnorderedSetIteratorErasePositionReturnsSuccessorRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorEraseMultisetErasesOnlyTheSelectedDuplicateRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorEraseHalfOpenRangeReturnsLastRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorEraseEmptyAndEntireMultisetRangesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorErasePointerReceiverRejected', 'TranslateTest.CoreV2UnorderedSetIteratorEraseDifferentKeyIteratorRejected', 'TranslateTest.CoreV2UnorderedSetIteratorEraseCustomHashRangeRejected', 'TranslateTest.CoreV2UnorderedSetIteratorEraseCustomEqualityPositionRejected'])
        self.assertEqual(evidence['original_test_names'], 8472)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-iterator-erase-position-returns-successor', output)
        self.assertIn(b'unordered-set-iterator-erase-multiset-erases-only-the-selected-duplicate', output)

    def test_unordered_set_iterator_erase_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_iterator_erase_history(self.unordered_set_iterator_erase_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_iterator_erase_registered, self.unordered_set_iterator_erase_suffix + b' ')


    def test_unordered_set_equal_range_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_equal_range_original + self.unordered_set_equal_range_suffix, self.unordered_set_equal_range_registered)
        self.assertEqual(self.adapter.restore_unordered_set_equal_range_history(self.unordered_set_equal_range_original),
                         self.unordered_set_iterator_erase_original)
        output, evidence = self.extractor.extract(self.unordered_set_equal_range_original, ['TranslateTest.CoreV2UnorderedSetEqualRangeUniqueRangeHasOneElementRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualRangeAbsentKeyHasTwoEndpointsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualRangeDuplicateRangePreservesCollidingGroupsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualRangeReceiverAndKeyEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualRangeDifferentKeyRejected', 'TranslateTest.CoreV2UnorderedSetEqualRangeCustomHashRejected', 'TranslateTest.CoreV2UnorderedSetEqualRangeCustomEqualityRejected', 'TranslateTest.CoreV2UnorderedSetEqualRangePointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8480)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-equal-range-unique-range-has-one-element', output)
        self.assertIn(b'unordered-set-equal-range-duplicate-range-preserves-colliding-groups', output)

    def test_unordered_set_equal_range_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_equal_range_history(self.unordered_set_equal_range_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_equal_range_registered, self.unordered_set_equal_range_suffix + b' ')


    def test_unordered_set_local_iterators_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_local_iterators_original + self.unordered_set_local_iterators_suffix, self.unordered_set_local_iterators_registered)
        self.assertEqual(self.adapter.restore_unordered_set_local_iterators_history(self.unordered_set_local_iterators_original),
                         self.unordered_set_equal_range_original)
        output, evidence = self.extractor.extract(self.unordered_set_local_iterators_original, ['TranslateTest.CoreV2UnorderedSetLocalIteratorsBucketBoundaryKeepsCollisionsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsConstBucketIterationSeesDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsEmptyBucketAndCopiedLocalEndRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsReceiverAndBucketEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsPointerReceiverRejected', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsDifferentKeyRejected', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsCustomHashRejected', 'TranslateTest.CoreV2UnorderedSetLocalIteratorsCustomEqualityRejected'])
        self.assertEqual(evidence['original_test_names'], 8488)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-local-iterators-bucket-boundary-keeps-collisions', output)
        self.assertIn(b'unordered-set-local-iterators-const-bucket-iteration-sees-duplicates', output)

    def test_unordered_set_local_iterators_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_local_iterators_history(self.unordered_set_local_iterators_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_local_iterators_registered, self.unordered_set_local_iterators_suffix + b' ')


    def test_unordered_set_emplacement_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_emplacement_original + self.unordered_set_emplacement_suffix, self.unordered_set_emplacement_registered)
        self.assertEqual(self.adapter.restore_unordered_set_emplacement_history(self.unordered_set_emplacement_original),
                         self.unordered_set_local_iterators_original)
        output, evidence = self.extractor.extract(self.unordered_set_emplacement_original, ['TranslateTest.CoreV2UnorderedSetEmplacementUniqueEmplacementReturnsResultRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEmplacementEquivalentHintBeforeExistingAfterGrowthRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEmplacementHintedValueInitializationAndUnrelatedHintRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEmplacementReceiverHintAndArgumentEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEmplacementDifferentConstructorTypeRejected', 'TranslateTest.CoreV2UnorderedSetEmplacementDifferentKeyTypeRejected', 'TranslateTest.CoreV2UnorderedSetEmplacementCustomHashRejected', 'TranslateTest.CoreV2UnorderedSetEmplacementPointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8496)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-emplacement-unique-emplacement-returns-result', output)
        self.assertIn(b'unordered-set-emplacement-equivalent-hint-before-existing-after-growth', output)

    def test_unordered_set_emplacement_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_emplacement_history(self.unordered_set_emplacement_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_emplacement_registered, self.unordered_set_emplacement_suffix + b' ')


    def test_unordered_set_equality_swap_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_equality_swap_original + self.unordered_set_equality_swap_suffix, self.unordered_set_equality_swap_registered)
        self.assertEqual(self.adapter.restore_unordered_set_equality_swap_history(self.unordered_set_equality_swap_original),
                         self.unordered_set_emplacement_original)
        output, evidence = self.extractor.extract(self.unordered_set_equality_swap_original, ['TranslateTest.CoreV2UnorderedSetEqualitySwapUniqueEqualityIgnoresBucketsAndOrderRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualitySwapMultisetEqualityChecksEachMultiplicityRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualitySwapMemberSwapPreservesStorageAndElementAddressesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualitySwapQualifiedAndAdlMultisetSwapRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetEqualitySwapOtherKeyEqualityRejected', 'TranslateTest.CoreV2UnorderedSetEqualitySwapCustomPolicyEqualityRejected', 'TranslateTest.CoreV2UnorderedSetEqualitySwapPointerMemberSwapRejected', 'TranslateTest.CoreV2UnorderedSetEqualitySwapCustomPolicyFreeSwapRejected'])
        self.assertEqual(evidence['original_test_names'], 8504)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-equality-swap-unique-equality-ignores-buckets-and-order', output)
        self.assertIn(b'unordered-set-equality-swap-multiset-equality-checks-each-multiplicity', output)

    def test_unordered_set_equality_swap_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_equality_swap_history(self.unordered_set_equality_swap_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_equality_swap_registered, self.unordered_set_equality_swap_suffix + b' ')


    def test_unordered_set_pointer_range_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_pointer_range_original + self.unordered_set_pointer_range_suffix, self.unordered_set_pointer_range_registered)
        self.assertEqual(self.adapter.restore_unordered_set_pointer_range_history(self.unordered_set_pointer_range_original),
                         self.unordered_set_equality_swap_original)
        output, evidence = self.extractor.extract(self.unordered_set_pointer_range_original, ['TranslateTest.CoreV2UnorderedSetPointerRangeUniqueRangeInsertsDistinctKeysRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerRangeMultisetConstRangeRetainsCountsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerRangeEmptyRangeKeepsBucketsAndValuesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerRangeReceiverAndBothEndpointsEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerRangeConvertedLongRangeRejected', 'TranslateTest.CoreV2UnorderedSetPointerRangeClassIteratorRangeRejected', 'TranslateTest.CoreV2UnorderedSetPointerRangeCustomPolicyRangeRejected', 'TranslateTest.CoreV2UnorderedSetPointerRangePointerReceiverRejected'])
        self.assertEqual(evidence['original_test_names'], 8512)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-pointer-range-unique-range-inserts-distinct-keys', output)
        self.assertIn(b'unordered-set-pointer-range-multiset-const-range-retains-counts', output)

    def test_unordered_set_pointer_range_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_pointer_range_history(self.unordered_set_pointer_range_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_pointer_range_registered, self.unordered_set_pointer_range_suffix + b' ')


    def test_unordered_set_pointer_construction_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_pointer_construction_original + self.unordered_set_pointer_construction_suffix, self.unordered_set_pointer_construction_registered)
        self.assertEqual(self.adapter.restore_unordered_set_pointer_construction_history(self.unordered_set_pointer_construction_original),
                         self.unordered_set_pointer_range_original)
        output, evidence = self.extractor.extract(self.unordered_set_pointer_construction_original, ['TranslateTest.CoreV2UnorderedSetPointerConstructionUniqueRangeConstructionDeduplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerConstructionMultisetConstructionRetainsDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerConstructionEmptyRangeUsesEmptyDefaultStateRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerConstructionRangeArgumentsAreEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetPointerConstructionExplicitBucketCountAndPoliciesRejected', 'TranslateTest.CoreV2UnorderedSetPointerConstructionConvertedShortRangeRejected', 'TranslateTest.CoreV2UnorderedSetPointerConstructionCustomHashRangeRejected', 'TranslateTest.CoreV2UnorderedSetPointerConstructionClassIteratorRangeRejected'])
        self.assertEqual(evidence['original_test_names'], 8520)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-pointer-construction-unique-range-construction-deduplicates', output)
        self.assertIn(b'unordered-set-pointer-construction-multiset-construction-retains-duplicates', output)

    def test_unordered_set_pointer_construction_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_pointer_construction_history(self.unordered_set_pointer_construction_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_pointer_construction_registered, self.unordered_set_pointer_construction_suffix + b' ')


    def test_unordered_set_owner_move_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_owner_move_original + self.unordered_set_owner_move_suffix, self.unordered_set_owner_move_registered)
        self.assertEqual(self.adapter.restore_unordered_set_owner_move_history(self.unordered_set_owner_move_original),
                         self.unordered_set_pointer_construction_original)
        output, evidence = self.extractor.extract(self.unordered_set_owner_move_original, ['TranslateTest.CoreV2UnorderedSetOwnerMoveUniqueConstructionTransfersNodesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerMoveEmptyMultisetConstructionKeepsReservationRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerMoveMultisetAssignmentTransfersDuplicatesAndReturnsReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerMoveQualifiedAssignmentKeepsEmptySourceBucketsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerMoveExplicitAllocatorConstructionRejected', 'TranslateTest.CoreV2UnorderedSetOwnerMoveOtherKeyConstructionRejected', 'TranslateTest.CoreV2UnorderedSetOwnerMoveCustomPolicyAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetOwnerMoveMultisetMoveWithExplicitAllocatorRejected'])
        self.assertEqual(evidence['original_test_names'], 8528)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-owner-move-unique-construction-transfers-nodes', output)
        self.assertIn(b'unordered-set-owner-move-empty-multiset-construction-keeps-reservation', output)

    def test_unordered_set_owner_move_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_owner_move_history(self.unordered_set_owner_move_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_owner_move_registered, self.unordered_set_owner_move_suffix + b' ')


    def test_unordered_set_owner_copy_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_owner_copy_original + self.unordered_set_owner_copy_suffix, self.unordered_set_owner_copy_registered)
        self.assertEqual(self.adapter.restore_unordered_set_owner_copy_history(self.unordered_set_owner_copy_original),
                         self.unordered_set_owner_move_original)
        output, evidence = self.extractor.extract(self.unordered_set_owner_copy_original, ['TranslateTest.CoreV2UnorderedSetOwnerCopyUniqueConstSourceOwnsDistinctNodesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerCopyMultisetCopyPreservesCountsAndIndependentEraseRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerCopyReservedEmptyCopyKeepsBucketPolicyRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerCopyReferenceProducingSourceEvaluatedOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetOwnerCopyExplicitAllocatorCopyRejected', 'TranslateTest.CoreV2UnorderedSetOwnerCopyOtherKeyCopyRejected', 'TranslateTest.CoreV2UnorderedSetOwnerCopyCustomEqualityCopyRejected', 'TranslateTest.CoreV2UnorderedSetOwnerCopyExplicitAllocatorMultisetCopyRejected'])
        self.assertEqual(evidence['original_test_names'], 8536)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-owner-copy-unique-const-source-owns-distinct-nodes', output)
        self.assertIn(b'unordered-set-owner-copy-multiset-copy-preserves-counts-and-independent-erase', output)

    def test_unordered_set_owner_copy_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_owner_copy_history(self.unordered_set_owner_copy_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_owner_copy_registered, self.unordered_set_owner_copy_suffix + b' ')


    def test_unordered_set_copy_assignment_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_copy_assignment_original + self.unordered_set_copy_assignment_suffix, self.unordered_set_copy_assignment_registered)
        self.assertEqual(self.adapter.restore_unordered_set_copy_assignment_history(self.unordered_set_copy_assignment_original),
                         self.unordered_set_owner_copy_original)
        output, evidence = self.extractor.extract(self.unordered_set_copy_assignment_original, ['TranslateTest.CoreV2UnorderedSetCopyAssignmentUniqueCopyAssignmentReusesDestinationBucketsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentMultisetCopyAssignmentGroupsCollidingDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentExplicitCopyAssignmentEvaluatesReceiverBeforeSourceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentSelfCopyPreservesAddressesAndEquivalentRangesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentCustomHashCopyAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentCustomEqualityMultisetCopyAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentUnsignedKeyCopyAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetCopyAssignmentLongKeyMultisetCopyAssignmentRejected'])
        self.assertEqual(evidence['original_test_names'], 8544)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-copy-assignment-unique-copy-assignment-reuses-destination-buckets', output)
        self.assertIn(b'unordered-set-copy-assignment-multiset-copy-assignment-groups-colliding-duplicates', output)

    def test_unordered_set_copy_assignment_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_copy_assignment_history(self.unordered_set_copy_assignment_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_copy_assignment_registered, self.unordered_set_copy_assignment_suffix + b' ')


    def test_unordered_set_list_assignment_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_list_assignment_original + self.unordered_set_list_assignment_suffix, self.unordered_set_list_assignment_registered)
        self.assertEqual(self.adapter.restore_unordered_set_list_assignment_history(self.unordered_set_list_assignment_original),
                         self.unordered_set_copy_assignment_original)
        output, evidence = self.extractor.extract(self.unordered_set_list_assignment_original, ['TranslateTest.CoreV2UnorderedSetListAssignmentUniqueListAssignmentKeepsBucketsAndLoadFactorRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetListAssignmentMultiListAssignmentGroupsCollidingDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetListAssignmentExplicitEmptyListAssignmentClearsReservedStorageRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetListAssignmentListAssignmentEvaluatesBracedInputBeforeReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetListAssignmentCustomHashListAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetListAssignmentCustomEqualityMultiListAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetListAssignmentLongKeyListAssignmentRejected', 'TranslateTest.CoreV2UnorderedSetListAssignmentShortKeyMultiListAssignmentRejected'])
        self.assertEqual(evidence['original_test_names'], 8552)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-list-assignment-unique-list-assignment-keeps-buckets-and-load-factor', output)
        self.assertIn(b'unordered-set-list-assignment-multi-list-assignment-groups-colliding-duplicates', output)

    def test_unordered_set_list_assignment_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_list_assignment_history(self.unordered_set_list_assignment_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_list_assignment_registered, self.unordered_set_list_assignment_suffix + b' ')


    def test_unordered_set_iterator_range_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_iterator_range_original + self.unordered_set_iterator_range_suffix, self.unordered_set_iterator_range_registered)
        self.assertEqual(self.adapter.restore_unordered_set_iterator_range_history(self.unordered_set_iterator_range_original),
                         self.unordered_set_list_assignment_original)
        output, evidence = self.extractor.extract(self.unordered_set_iterator_range_original, ['TranslateTest.CoreV2UnorderedSetIteratorRangeUniqueInsertsConstMultisetIteratorRangeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorRangeMultiInsertsOneElementFromAGlobalIteratorRangeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorRangeMultiInsertsOneLocalBucketWithDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorRangeUniqueConstructsFromMultisetGlobalIteratorsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorRangeMultiConstructsFromLocalIteratorsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorRangeEqualNonnullEndpointsFormAnEmptyIteratorRangeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetIteratorRangeUnrelatedVectorIteratorInsertionRejected', 'TranslateTest.CoreV2UnorderedSetIteratorRangeUnrelatedListIteratorConstructionRejected', 'TranslateTest.CoreV2UnorderedSetIteratorRangeUnsignedKeyIteratorInsertionRejected', 'TranslateTest.CoreV2UnorderedSetIteratorRangeCustomHashIteratorConstructionRejected'])
        self.assertEqual(evidence['original_test_names'], 8562)
        self.assertEqual(evidence['selected_count'], 10)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-iterator-range-unique-inserts-const-multiset-iterator-range', output)
        self.assertIn(b'unordered-set-iterator-range-multi-constructs-from-local-iterators', output)

    def test_unordered_set_iterator_range_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_iterator_range_history(self.unordered_set_iterator_range_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_iterator_range_registered, self.unordered_set_iterator_range_suffix + b' ')


    def test_unordered_set_merge_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_merge_original + self.unordered_set_merge_suffix, self.unordered_set_merge_registered)
        self.assertEqual(self.adapter.restore_unordered_set_merge_history(self.unordered_set_merge_original),
                         self.unordered_set_iterator_range_original)
        output, evidence = self.extractor.extract(self.unordered_set_merge_original, ['TranslateTest.CoreV2UnorderedSetMergeUniqueMergePreservesNodeAndRejectedDuplicateAddressesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMergeUniqueMergesOneCopyFromEachNewMultisetKeyRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMergeMultiMergesRvalueUniqueOwnerWithoutReallocatingNodesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMergeMultiMergeGroupsEquivalentKeysInCollidingBucketRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMergeCustomHashMergeSourceRejected', 'TranslateTest.CoreV2UnorderedSetMergeCustomHashMergeDestinationRejected', 'TranslateTest.CoreV2UnorderedSetMergeCustomEqualityMergeDestinationRejected', 'TranslateTest.CoreV2UnorderedSetMergeLongKeyMergeRejected'])
        self.assertEqual(evidence['original_test_names'], 8570)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-merge-unique-merge-preserves-node-and-rejected-duplicate-addresses', output)
        self.assertIn(b'unordered-set-merge-multi-merge-groups-equivalent-keys-in-colliding-bucket', output)

    def test_unordered_set_merge_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_merge_history(self.unordered_set_merge_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_merge_registered, self.unordered_set_merge_suffix + b' ')


    def test_unordered_set_observers_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_observers_original + self.unordered_set_observers_suffix, self.unordered_set_observers_registered)
        self.assertEqual(self.adapter.restore_unordered_set_observers_history(self.unordered_set_observers_original),
                         self.unordered_set_merge_original)
        output, evidence = self.extractor.extract(self.unordered_set_observers_original, ['TranslateTest.CoreV2UnorderedSetObserversSetAllocatorAllocatesAndReleasesIntegerStorageRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetObserversMultisetAllocatorEvaluatesArrowReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetObserversHashFunctionOnArrowReceiverUsesPinnedSignedHashRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetObserversKeyEqualityOnArrowReceiverComparesDefaultIntegerKeysRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetObserversCustomHashArrowObserverRejected', 'TranslateTest.CoreV2UnorderedSetObserversCustomEqualityArrowObserverRejected', 'TranslateTest.CoreV2UnorderedSetObserversDifferentKeyAllocatorObserverRejected', 'TranslateTest.CoreV2UnorderedSetObserversCustomHashAllocatorObserverRejected'])
        self.assertEqual(evidence['original_test_names'], 8578)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-observers-set-allocator-allocates-and-releases-integer-storage', output)
        self.assertIn(b'unordered-set-observers-key-equality-on-arrow-receiver-compares-default-integer-keys', output)

    def test_unordered_set_observers_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_observers_history(self.unordered_set_observers_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_observers_registered, self.unordered_set_observers_suffix + b' ')


    def test_unordered_set_storage_construction_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_storage_construction_original + self.unordered_set_storage_construction_suffix, self.unordered_set_storage_construction_registered)
        self.assertEqual(self.adapter.restore_unordered_set_storage_construction_history(self.unordered_set_storage_construction_original),
                         self.unordered_set_observers_original)
        output, evidence = self.extractor.extract(self.unordered_set_storage_construction_original, ['TranslateTest.CoreV2UnorderedSetStorageConstructionSetBucketCountDefaultPoliciesEvaluateCountOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetStorageConstructionMultisetExplicitHashEqualAndAllocatorBucketConstructionRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetStorageConstructionSetBucketDelegationWithDefaultAllocatorEvaluatesArgumentOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetStorageConstructionMultisetAllocatorOnlyStartsEmptyAndOwnsInsertedKeysRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetStorageConstructionSetCopyWithDefaultAllocatorOwnsADistinctNodeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetStorageConstructionMultisetMoveWithEqualDefaultAllocatorRetainsNodeAddressRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetStorageConstructionSetCustomHashBucketConstructionRejected', 'TranslateTest.CoreV2UnorderedSetStorageConstructionMultisetCustomEqualityBucketConstructionRejected', 'TranslateTest.CoreV2UnorderedSetStorageConstructionUnapprovedKeyAllocatorConstructionRejected', 'TranslateTest.CoreV2UnorderedSetStorageConstructionUnapprovedKeyBucketConstructionRejected'])
        self.assertEqual(evidence['original_test_names'], 8588)
        self.assertEqual(evidence['selected_count'], 10)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-storage-construction-set-bucket-count-default-policies-evaluate-count-once', output)
        self.assertIn(b'unordered-set-storage-construction-multiset-move-with-equal-default-allocator-retains-node-address', output)

    def test_unordered_set_storage_construction_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_storage_construction_history(self.unordered_set_storage_construction_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_storage_construction_registered, self.unordered_set_storage_construction_suffix + b' ')


    def test_unordered_set_node_handles_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_node_handles_original + self.unordered_set_node_handles_suffix, self.unordered_set_node_handles_registered)
        self.assertEqual(self.adapter.restore_unordered_set_node_handles_history(self.unordered_set_node_handles_original),
                         self.unordered_set_storage_construction_original)
        output, evidence = self.extractor.extract(self.unordered_set_node_handles_original, ['TranslateTest.CoreV2UnorderedSetNodeHandlesUniqueExtractsKeyAndMutatesDetachedValueRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesMultisetExtractsOneNodeFromConstIteratorRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesDefaultAndMissingKeyHandlesStayEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesSetAndMultisetHandleMovesReleaseThePreviousNodeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesSwappedNodeRetainsAllocatorAndValueRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesUniqueDuplicateReturnsOwningNodeForRetryRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesUniqueHintedDuplicateRetainsTheSuppliedHandleRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesMultisetInsertNodePreservesStorageAndRehashesMutatedKeyRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesMultisetHintInsertsTheTransferredNodeWithItsEquivalentsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesInsertingEmptyHandleReturnsEndInBothContainersRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetNodeHandlesOtherKeySetNodeExtractionRejected', 'TranslateTest.CoreV2UnorderedSetNodeHandlesCustomPolicyMultisetNodeExtractionRejected', 'TranslateTest.CoreV2UnorderedSetNodeHandlesOtherKeyDefaultNodeHandleRejected', 'TranslateTest.CoreV2UnorderedSetNodeHandlesCustomHashUniqueHandleInsertRejected', 'TranslateTest.CoreV2UnorderedSetNodeHandlesCustomEqualityMultisetHandleInsertRejected'])
        self.assertEqual(evidence['original_test_names'], 8603)
        self.assertEqual(evidence['selected_count'], 15)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-node-handles-unique-extracts-key-and-mutates-detached-value', output)
        self.assertIn(b'unordered-set-node-handles-multiset-insert-node-preserves-storage-and-rehashes-mutated-key', output)

    def test_unordered_set_node_handles_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_node_handles_history(self.unordered_set_node_handles_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_node_handles_registered, self.unordered_set_node_handles_suffix + b' ')


    def test_unordered_set_bucket_receivers_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_bucket_receivers_original + self.unordered_set_bucket_receivers_suffix, self.unordered_set_bucket_receivers_registered)
        self.assertEqual(self.adapter.restore_unordered_set_bucket_receivers_history(self.unordered_set_bucket_receivers_original),
                         self.unordered_set_node_handles_original)
        output, evidence = self.extractor.extract(self.unordered_set_bucket_receivers_original, ['TranslateTest.CoreV2UnorderedSetBucketReceiversCapacityConstantsEvaluateTheConstPointerReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetBucketReceiversPointerCapacityAndLoadObserversUseTheLiveTableRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetBucketReceiversPointerReserveAndRehashEvaluateTheReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetBucketReceiversPointerBucketAndSizeFindAnEquivalentKeyGroupRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetBucketReceiversPointerSetterPreservesTheLiveLoadLimitRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetBucketReceiversOtherKeyPointerCapacityObserverRejected', 'TranslateTest.CoreV2UnorderedSetBucketReceiversCustomHashPointerRehashRejected', 'TranslateTest.CoreV2UnorderedSetBucketReceiversCustomEqualityPointerLoadLimitRejected'])
        self.assertEqual(evidence['original_test_names'], 8611)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-bucket-receivers-capacity-constants-evaluate-the-const-pointer-receiver', output)
        self.assertIn(b'unordered-set-bucket-receivers-pointer-setter-preserves-the-live-load-limit', output)

    def test_unordered_set_bucket_receivers_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_bucket_receivers_history(self.unordered_set_bucket_receivers_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_bucket_receivers_registered, self.unordered_set_bucket_receivers_suffix + b' ')


    def test_unordered_set_member_receivers_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_member_receivers_original + self.unordered_set_member_receivers_suffix, self.unordered_set_member_receivers_registered)
        self.assertEqual(self.adapter.restore_unordered_set_member_receivers_history(self.unordered_set_member_receivers_original),
                         self.unordered_set_bucket_receivers_original)
        output, evidence = self.extractor.extract(self.unordered_set_member_receivers_original, ['TranslateTest.CoreV2UnorderedSetMemberReceiversPointerRangeEvaluatesBucketCountOnceBeforeInsertingRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversIteratorRangeWithPinnedPoliciesRetainsEquivalentKeysRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversListWithBucketCountAndAllocatorCoalescesDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversListWithAllPoliciesPreservesDuplicateKeyGroupRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversEmptyRangeKeepsExplicitBucketStorageRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversMemberPointerExpressionsEvaluateOnceRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversConstPointerGlobalAndLocalIterationRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversPointerRangeInsertionAndClearPreserveKeyMultiplicityRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversPointerSwapRetainsElementAddressAndLiveBucketsRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetMemberReceiversRangeWithUserHashRejected', 'TranslateTest.CoreV2UnorderedSetMemberReceiversBucketedListWithUnapprovedKeyTypeRejected', 'TranslateTest.CoreV2UnorderedSetMemberReceiversUserHashPointerMemberRejected', 'TranslateTest.CoreV2UnorderedSetMemberReceiversUnapprovedKeyPointerMemberRejected'])
        self.assertEqual(evidence['original_test_names'], 8624)
        self.assertEqual(evidence['selected_count'], 13)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-member-receivers-pointer-range-evaluates-bucket-count-once-before-inserting', output)
        self.assertIn(b'unordered-set-member-receivers-member-pointer-expressions-evaluate-once', output)

    def test_unordered_set_member_receivers_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_member_receivers_history(self.unordered_set_member_receivers_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_member_receivers_registered, self.unordered_set_member_receivers_suffix + b' ')


    def test_unordered_multimap_owners_retains_published_bodies(self):
        self.assertEqual(self.unordered_multimap_owners_original + self.unordered_multimap_owners_suffix, self.unordered_multimap_owners_registered)
        self.assertEqual(self.adapter.restore_unordered_multimap_owners_history(self.unordered_multimap_owners_original),
                         self.unordered_set_member_receivers_original)
        output, evidence = self.extractor.extract(self.unordered_multimap_owners_original, ['TranslateTest.CoreV2UnorderedMultimapOwnersDefaultConstructionAndCapacityRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapOwnersListKeepsAllEquivalentKeysAndRemovesTheGroupRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapOwnersCopyOwnsDistinctNodesAfterSourceClearRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapOwnersBucketSizePreservesDuplicateMultiplicityAfterRehashRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapOwnersComparisonRequiresMappedValueMultiplicitiesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapOwnersCustomHashOwnerRejected', 'TranslateTest.CoreV2UnorderedMultimapOwnersUnapprovedMappedTypeRejected'])
        self.assertEqual(evidence['original_test_names'], 8631)
        self.assertEqual(evidence['selected_count'], 7)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-multimap-owners-default-construction-and-capacity', output)
        self.assertIn(b'unordered-multimap-owners-comparison-requires-mapped-value-multiplicities', output)

    def test_unordered_multimap_owners_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_multimap_owners_history(self.unordered_multimap_owners_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_multimap_owners_registered, self.unordered_multimap_owners_suffix + b' ')


    def test_unordered_multimap_iteration_and_transfer_retains_published_bodies(self):
        self.assertEqual(self.unordered_multimap_iteration_and_transfer_original + self.unordered_multimap_iteration_and_transfer_suffix, self.unordered_multimap_iteration_and_transfer_registered)
        self.assertEqual(self.adapter.restore_unordered_multimap_iteration_and_transfer_history(self.unordered_multimap_iteration_and_transfer_original),
                         self.unordered_multimap_owners_original)
        output, evidence = self.extractor.extract(self.unordered_multimap_iteration_and_transfer_original, ['TranslateTest.CoreV2UnorderedMultimapIterationAndTransferIteratorFindAndTraverseWithUniqueOwnerInSameTranslationRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferIteratorConstEqualRangeReachesTheEndOfTheGroupRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferIteratorErasePositionAndRangeUnlinkOnlySelectedNodesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferIteratorLocalConstIterationSeesTheMatchingBucketRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferModifierEmplaceAndInsertReturnTheNewEquivalentNodeRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferModifierHintedInsertionKeepsEquivalentKeysContiguousRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferModifierIteratorInputConvertsBetweenCompatibleMappedOwnersRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferModifierMergeIntoMultiPreservesPayloadAddressesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferModifierMergeIntoUniqueLeavesEquivalentUntransferredNodesRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferIteratorCustomPolicyIteratorRejected', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferIteratorOtherMappedTypeIteratorRejected', 'TranslateTest.CoreV2UnorderedMultimapIterationAndTransferModifierMergeFromUserHashPolicyRejected'])
        self.assertEqual(evidence['original_test_names'], 8643)
        self.assertEqual(evidence['selected_count'], 12)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-multimap-iteration-and-transfer-iterator-find-and-traverse-with-unique-owner-in-same-translation', output)
        self.assertIn(b'unordered-multimap-iteration-and-transfer-modifier-merge-into-unique-leaves-equivalent-untransferred-nodes', output)

    def test_unordered_multimap_iteration_and_transfer_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_multimap_iteration_and_transfer_history(self.unordered_multimap_iteration_and_transfer_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_multimap_iteration_and_transfer_registered, self.unordered_multimap_iteration_and_transfer_suffix + b' ')


    def test_associative_node_and_observer_retains_published_bodies(self):
        self.assertEqual(self.associative_node_and_observer_original + self.associative_node_and_observer_suffix, self.associative_node_and_observer_registered)
        self.assertEqual(self.adapter.restore_associative_node_and_observer_history(self.associative_node_and_observer_original),
                         self.unordered_multimap_iteration_and_transfer_original)
        output, evidence = self.extractor.extract(self.associative_node_and_observer_original, ['TranslateTest.CoreV2UnorderedMultimapNodeHandlesExtractIteratorAndReinsertMutatedMappedValueRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapNodeHandlesRejectedUniqueInsertionReturnsHandleForMultimapRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapNodeHandlesEmptyExtractionAndInsertionReturnEndRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedPointerObserversMapSizeEmptyAndMaximumRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedPointerObserversMultimapPointerKeyComparatorEvaluatesReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedPointerObserversMultisetPointerValueComparatorRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedMultimapNodeHandlesMappedNodeWithUnapprovedValueTypeRejected', 'TranslateTest.CoreV2OrderedPointerObserversCustomOrderedComparePointerRejected'])
        self.assertEqual(evidence['original_test_names'], 8651)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-multimap-node-handles-extract-iterator-and-reinsert-mutated-mapped-value', output)
        self.assertIn(b'ordered-pointer-observers-multiset-pointer-value-comparator', output)

    def test_associative_node_and_observer_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_associative_node_and_observer_history(self.associative_node_and_observer_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.associative_node_and_observer_registered, self.associative_node_and_observer_suffix + b' ')


    def test_unordered_set_unsigned_key_retains_published_bodies(self):
        self.assertEqual(self.unordered_set_unsigned_key_original + self.unordered_set_unsigned_key_suffix, self.unordered_set_unsigned_key_registered)
        self.assertEqual(self.adapter.restore_unordered_set_unsigned_key_history(self.unordered_set_unsigned_key_original),
                         self.associative_node_and_observer_original)
        output, evidence = self.extractor.extract(self.unordered_set_unsigned_key_original, ['TranslateTest.CoreV2UnorderedSetUnsignedKeyLargeKeyBucketAndLocalIteratorRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetUnsignedKeyBucketedRangesAndCopyUseUnsignedKeyRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetUnsignedKeyNodeTransferRetainsUnsignedPayloadAndAddressRunAtBothOptimizations', 'TranslateTest.CoreV2UnorderedSetUnsignedKeyAlternateUnsignedHashRemainsUnsupportedRejected'])
        self.assertEqual(evidence['original_test_names'], 8655)
        self.assertEqual(evidence['selected_count'], 4)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'unordered-set-unsigned-key-large-key-bucket-and-local-iterator', output)
        self.assertIn(b'unordered-set-unsigned-key-node-transfer-retains-unsigned-payload-and-address', output)

    def test_unordered_set_unsigned_key_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_unordered_set_unsigned_key_history(self.unordered_set_unsigned_key_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.unordered_set_unsigned_key_registered, self.unordered_set_unsigned_key_suffix + b' ')


    def test_ordered_set_core_retains_published_bodies(self):
        self.assertEqual(self.ordered_set_core_original + self.ordered_set_core_suffix, self.ordered_set_core_registered)
        self.assertEqual(self.adapter.restore_ordered_set_core_history(self.ordered_set_core_original),
                         self.unordered_set_unsigned_key_original)
        output, evidence = self.extractor.extract(self.ordered_set_core_original, ['TranslateTest.CoreV2OrderedSetListAndLookupListBalancesSortedKeysAndDeduplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetListAndLookupPointerCountAfterLeftAndRightRotationsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetListAndLookupListDuplicatesReleaseEachOwnedNodeOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetInsertResultInsertionResultDistinguishesADuplicateKeyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetInsertResultPointerInsertionUsesTheCheckedReceiverOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetIteratorOrderedRangeAndInsertionPreserveExistingPositionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetIteratorPointerEndpointsAndPostfixTraversalRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetIteratorEmptyIteratorsUseTheSameOwnerSentinelRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetBoundsBoundsOnKeysAndGapsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetBoundsConstPointerLookupMatchesExtremeKeysRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetBoundsEmptyLookupEvaluatesReceiverAndKeyOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetListAndLookupCustomOrderKeepsExistingSourceGuardRejected', 'TranslateTest.CoreV2OrderedSetInsertResultUnapprovedCustomComparatorForScalarInsertionRejected', 'TranslateTest.CoreV2OrderedSetIteratorStatefulOrderingRemainsOutsideTheCheckedIteratorTypeRejected', 'TranslateTest.CoreV2OrderedSetBoundsCustomOrderingDoesNotUseIntegerBoundsRejected'])
        self.assertEqual(evidence['original_test_names'], 8670)
        self.assertEqual(evidence['selected_count'], 15)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-set-list-and-lookup-list-balances-sorted-keys-and-deduplicates', output)
        self.assertIn(b'ordered-set-bounds-bounds-on-keys-and-gaps', output)

    def test_ordered_set_core_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_set_core_history(self.ordered_set_core_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_set_core_registered, self.ordered_set_core_suffix + b' ')


    def test_ordered_owner_operations_retains_published_bodies(self):
        self.assertEqual(self.ordered_owner_operations_original + self.ordered_owner_operations_suffix, self.ordered_owner_operations_registered)
        self.assertEqual(self.adapter.restore_ordered_owner_operations_history(self.ordered_owner_operations_original),
                         self.ordered_set_core_original)
        output, evidence = self.extractor.extract(self.ordered_owner_operations_original, ['TranslateTest.CoreV2OrderedSetErasureKeyEraseKeepsOtherNodeAddressesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetErasurePointerPositionAndRangeReturnTheRetainedSuccessorRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetErasureClearResetsOwnershipForReinsertionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetTransferMoveConstructionReparentsTheRootAndKeepsNodeAddressesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetTransferSwapThroughPointerRebindsEmptyAndNonemptySentinelsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetTransferSwapTwoLiveTreesPreservesTheirNodeOrderAndIdentityRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetEqualRangeFoundAndAbsentKeysUseTheSameTreeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetEqualRangeConstPointerEqualRangeEvaluatesTheKeyOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetEqualRangeEmptyEqualRangeUsesBothOwnerSentinelsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetEquivalentInsertionFollowsExistingNodesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetKeyErasureRemovesAllMatchesAndPreservesOtherNodesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetSwapWithASetIteratorInTheSameTranslationRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetCopyConstructionSetIndependentNodesAndSortedCopyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetCopyConstructionMultisetDuplicatesAndIndependentLifetimeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetCopyConstructionEmptyOwnersGetDistinctSentinelsAndNoAllocationsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetCopyAssignmentSetReusesOldNodeAllocationsAndReleasesOnlySurplusRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetCopyAssignmentMultisetReplacesEmptyAndShorterInputsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetCopyAssignmentAssignmentNotationAndExplicitMemberSequenceBothSidesOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetErasureCustomOrderingDoesNotUseIntegerErasureRejected', 'TranslateTest.CoreV2OrderedSetTransferTransparentComparatorDoesNotUseTheDefaultTransferRejected', 'TranslateTest.CoreV2OrderedSetEqualRangeTransparentComparatorOverloadRemainsGuardedRejected', 'TranslateTest.CoreV2OrderedMultisetCustomOrderingRemainsGuardedRejected', 'TranslateTest.CoreV2OrderedSetCopyConstructionCustomComparatorRemainsOutsideCheckedCopyRejected', 'TranslateTest.CoreV2OrderedSetCopyAssignmentCustomComparatorRemainsOutsideCheckedAssignmentRejected'])
        self.assertEqual(evidence['original_test_names'], 8694)
        self.assertEqual(evidence['selected_count'], 24)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-set-erasure-key-erase-keeps-other-node-addresses', output)
        self.assertIn(b'ordered-set-copy-assignment-multiset-replaces-empty-and-shorter-inputs', output)

    def test_ordered_owner_operations_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_owner_operations_history(self.ordered_owner_operations_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_owner_operations_registered, self.ordered_owner_operations_suffix + b' ')


    def test_ordered_maps_with_mixed_cursors_retains_published_bodies(self):
        self.assertEqual(self.ordered_maps_with_mixed_cursors_original + self.ordered_maps_with_mixed_cursors_suffix, self.ordered_maps_with_mixed_cursors_registered)
        self.assertEqual(self.adapter.restore_ordered_maps_with_mixed_cursors_history(self.ordered_maps_with_mixed_cursors_original),
                         self.ordered_owner_operations_original)
        output, evidence = self.extractor.extract(self.ordered_maps_with_mixed_cursors_original, ['TranslateTest.CoreV2OrderedMapDefaultMappedValueAndStableReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapPointerReceiverAndKeyEvaluatedOnceInOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapRepeatedSubscriptAllocatesAndDestroysOneNodePerKeyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapContainsIntegerKeysAfterInsertionAndPreservesSizeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapConstPointerCountEvaluatesReceiverAndKeyOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInitializerListConstructsOrderedMapWithFirstMappedValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMultimapInitializerListKeepsDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInitializerListDestroysEachMappedNodeOnlyOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapIteratorFindReturnsAMutableMappedLvalueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapIteratorConstIteratorTraversesBoundsAndDecrementsEndRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapIteratorMultimapEqualRangePreservesMappedDuplicateOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapErasureKeyEraseKeepsTheMappedAddressOfAnotherNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapErasureEraseConstIteratorSpanReturnsMutableSuccessorRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapErasureMultimapErasesEveryDuplicateAndClearReseatsSentinelRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInsertionMapInsertResultKeepsFirstValueOnADuplicateRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInsertionMultimapInsertReturnsMappedNodeInDuplicateOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInsertionMapPointerCallEvaluatesOwnerAndInputOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapScalarEmplacementTryEmplaceKeepsExistingValueAndDefaultsANewNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapScalarEmplacementInsertOrAssignReusesNodeAndReportsInsertionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapScalarEmplacementTryEmplaceEvaluatesArgumentsOnADuplicateRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapTransferMapCopyCreatesIndependentMappedObjectsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapTransferMapMoveKeepsReferencesAndTraversesNewSentinelRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapTransferMultimapSwapPreservesEquivalentNodeOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapCopyAssignmentReplacesAndOwnsMappedValuesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapCopyAssignmentMultimapRetainsDuplicatesAndClearsOldNodesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapCopyAssignmentPreservesOperandEvaluationOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAndSetCursorsKeepIndependentSentinelsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapIteratorConversionKeepsNodeAndConstViewRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapIteratorConversionMultimapMatchesDuplicatePositionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapIteratorConversionMixedEqualitySeesOwnerEndRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapNonDefaultKeyOrderRemainsOutsideCheckedSubscriptRejected', 'TranslateTest.CoreV2OrderedMapUnsupportedMappedTypeDoesNotAdmitCountRejected', 'TranslateTest.CoreV2OrderedMapStatefulPolicyRemainsOutsideCheckedMappedOwnerRejected', 'TranslateTest.CoreV2OrderedMapIteratorCustomMapIteratorReceiverKeepsSourceGuardRejected', 'TranslateTest.CoreV2OrderedMapErasureUnsupportedMappedStorageKeepsMutationGuardRejected', 'TranslateTest.CoreV2OrderedMapInsertionStatefulComparatorInsertionRemainsOutsideCheckedLayoutRejected', 'TranslateTest.CoreV2OrderedMapScalarEmplacementMappedForwardingConversionsRequireTheirOwnProofRejected', 'TranslateTest.CoreV2OrderedMapTransferStatefulOrderedMapComparatorHasSeparateTransferSemanticsRejected', 'TranslateTest.CoreV2OrderedMapCopyAssignmentCustomComparatorRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedMapIteratorConversionCustomComparatorRemainsUnsupportedRejected'])
        self.assertEqual(evidence['original_test_names'], 8734)
        self.assertEqual(evidence['selected_count'], 40)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-map-default-mapped-value-and-stable-reference', output)
        self.assertIn(b'ordered-map-iterator-conversion-mixed-equality-sees-owner-end', output)

    def test_ordered_maps_with_mixed_cursors_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_maps_with_mixed_cursors_history(self.ordered_maps_with_mixed_cursors_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_maps_with_mixed_cursors_registered, self.ordered_maps_with_mixed_cursors_suffix + b' ')


    def test_ordered_move_list_range_retains_published_bodies(self):
        self.assertEqual(self.ordered_move_list_range_original + self.ordered_move_list_range_suffix, self.ordered_move_list_range_registered)
        self.assertEqual(self.adapter.restore_ordered_move_list_range_history(self.ordered_move_list_range_original),
                         self.ordered_maps_with_mixed_cursors_original)
        output, evidence = self.extractor.extract(self.ordered_move_list_range_original, ['TranslateTest.CoreV2OrderedMapMoveAssignmentReleasesOldStorageAndKeepsIncomingNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMoveAssignmentEmptyMultimapRebindsSentinelRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetMoveAssignmentKeepsIncomingKeyAddressRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInsertListPreservesOldAndFirstNewMappedValuesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInsertListMultimapKeepsDuplicateInsertionOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetInsertListEvaluatesReceiverAndListOnceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetPointerRangeConstructionAndEmptyInsertRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetPointerRangeConstInputRetainsDuplicatesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapPointerRangeUsesFirstValueAndCopiesStorageRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapPointerRangeEvaluatesEndpointsOnceAndKeepsOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMoveAssignmentCustomComparatorRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedMapInsertListCustomComparatorRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedMapPointerRangeConvertingPairRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedSetPointerRangeCustomComparatorRemainsUnsupportedRejected'])
        self.assertEqual(evidence['original_test_names'], 8748)
        self.assertEqual(evidence['selected_count'], 14)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-map-move-assignment-releases-old-storage-and-keeps-incoming-node', output)
        self.assertIn(b'ordered-set-pointer-range-custom-comparator-remains-unsupported', output)

    def test_ordered_move_list_range_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_move_list_range_history(self.ordered_move_list_range_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_move_list_range_registered, self.ordered_move_list_range_suffix + b' ')


    def test_ordered_list_assignment_and_emplace_retains_published_bodies(self):
        self.assertEqual(self.ordered_list_assignment_and_emplace_original + self.ordered_list_assignment_and_emplace_suffix, self.ordered_list_assignment_and_emplace_registered)
        self.assertEqual(self.adapter.restore_ordered_list_assignment_and_emplace_history(self.ordered_list_assignment_and_emplace_original),
                         self.ordered_move_list_range_original)
        output, evidence = self.extractor.extract(self.ordered_list_assignment_and_emplace_original, ['TranslateTest.CoreV2OrderedMapListAssignmentReusesOnlySuccessfulUniqueInsertionsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapListAssignmentKeepsTheNewEquivalentOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetListAssignmentPreservesOperatorAndMemberEvaluationOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetEmptyListAssignmentReusesAnEmptyOwnerAfterClearRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetEmplaceExactIntReusesTheExistingKeyWithoutAllocatingRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetEmplaceDefaultDestroysARejectedZeroNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetEmplaceDefaultAndRvaluePreserveEquivalentOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapListAssignmentCustomComparatorRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedSetEmplaceConvertingKeyRemainsUnsupportedRejected'])
        self.assertEqual(evidence['original_test_names'], 8757)
        self.assertEqual(evidence['selected_count'], 9)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-map-list-assignment-reuses-only-successful-unique-insertions', output)
        self.assertIn(b'ordered-set-emplace-converting-key-remains-unsupported', output)

    def test_ordered_list_assignment_and_emplace_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_list_assignment_and_emplace_history(self.ordered_list_assignment_and_emplace_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_list_assignment_and_emplace_registered, self.ordered_list_assignment_and_emplace_suffix + b' ')


    def test_ordered_merge_comparison_and_map_emplace_retains_published_bodies(self):
        self.assertEqual(self.ordered_merge_comparison_and_map_emplace_original + self.ordered_merge_comparison_and_map_emplace_suffix, self.ordered_merge_comparison_and_map_emplace_registered)
        self.assertEqual(self.adapter.restore_ordered_merge_comparison_and_map_emplace_history(self.ordered_merge_comparison_and_map_emplace_original),
                         self.ordered_list_assignment_and_emplace_original)
        output, evidence = self.extractor.extract(self.ordered_merge_comparison_and_map_emplace_original, ['TranslateTest.CoreV2OrderedSetMergeTransfersOriginalAddressesWithoutAllocatingRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMergeMultimapKeepsConflictsInTheSourceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapMergeRvalueMapAppendsEquivalentNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetSelfMergePreservesItsNodesAndSizeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetComparisonConstPrefixAndAllRelationsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapComparisonUsesMappedValuesAfterEqualKeysRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapComparisonRetainsEquivalentNodeOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetComparisonDistinguishesDuplicateCountsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapEmplaceIntDoesNotReplaceAnExistingValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapEmplaceIntKeepsEquivalentInsertionOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMergeCustomComparatorRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedComparisonCustomComparatorRemainsUnsupportedRejected', 'TranslateTest.CoreV2OrderedMapEmplaceConvertingMappedArgumentRemainsUnsupportedRejected'])
        self.assertEqual(evidence['original_test_names'], 8770)
        self.assertEqual(evidence['selected_count'], 13)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-set-merge-transfers-original-addresses-without-allocating', output)
        self.assertIn(b'ordered-map-emplace-converting-mapped-argument-remains-unsupported', output)

    def test_ordered_merge_comparison_and_map_emplace_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_merge_comparison_and_map_emplace_history(self.ordered_merge_comparison_and_map_emplace_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_merge_comparison_and_map_emplace_registered, self.ordered_merge_comparison_and_map_emplace_suffix + b' ')


    def test_ordered_policies_and_iterator_ranges_retains_published_bodies(self):
        self.assertEqual(self.ordered_policies_and_iterator_ranges_original + self.ordered_policies_and_iterator_ranges_suffix, self.ordered_policies_and_iterator_ranges_registered)
        self.assertEqual(self.adapter.restore_ordered_policies_and_iterator_ranges_history(self.ordered_policies_and_iterator_ranges_original),
                         self.ordered_merge_comparison_and_map_emplace_original)
        output, evidence = self.extractor.extract(self.ordered_policies_and_iterator_ranges_original, ['TranslateTest.CoreV2OrderedSetIteratorRangeCopiesASubrangeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetRangeFromSetAndDuplicateIteratorsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMutableAndConstIteratorRangesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapIteratorRangeKeepsEquivalentOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetExplicitDefaultPoliciesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetExplicitDefaultPoliciesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapExplicitDefaultPoliciesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapExplicitDefaultPoliciesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetAllocatorObserverEvaluatesReceiverRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetAllocatorObserverOnEmptyOwnerRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAllocatorObserverRebindsToPublicValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapAllocatorObserverOnConstOwnerRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedRangeReverseIteratorGuardRejected', 'TranslateTest.CoreV2OrderedRangeForeignCursorGuardRejected', 'TranslateTest.CoreV2OrderedExplicitStatefulPolicyGuardRejected', 'TranslateTest.CoreV2OrderedAllocatorCopyConstructionGuardRejected', 'TranslateTest.CoreV2OrderedAllocatorObserverCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedAllocatorObserverOtherMappedTypeGuardRejected'])
        self.assertEqual(evidence['original_test_names'], 8788)
        self.assertEqual(evidence['selected_count'], 18)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-set-iterator-range-copies-a-subrange', output)
        self.assertIn(b'ordered-allocator-observer-custom-policy-guard', output)

    def test_ordered_policies_and_iterator_ranges_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_policies_and_iterator_ranges_history(self.ordered_policies_and_iterator_ranges_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_policies_and_iterator_ranges_registered, self.ordered_policies_and_iterator_ranges_suffix + b' ')


    def test_ordered_node_and_value_compare_retains_published_bodies(self):
        self.assertEqual(self.ordered_node_and_value_compare_original + self.ordered_node_and_value_compare_suffix, self.ordered_node_and_value_compare_registered)
        self.assertEqual(self.adapter.restore_ordered_node_and_value_compare_history(self.ordered_node_and_value_compare_original),
                         self.ordered_policies_and_iterator_ranges_original)
        output, evidence = self.extractor.extract(self.ordered_node_and_value_compare_original, ['TranslateTest.CoreV2OrderedSetExtractKeepsDetachedNodeUntilHandleDestructionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetExtractPositionAndMissingKeyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapExtractAllowsDetachedKeyAndValueChangesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapExtractKeepsEquivalentNodeOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetNodeInsertionKeepsAddressAndReturnsEndForEmptyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapNodeConflictReturnsOwnerWithMutableKeyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetNodeInsertionTransfersBetweenOwnerKindsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapNodeInsertAppendsAnEquivalentNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedNodeCaseRetainsReinsertionGuardRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedValueCompareIgnoresTheMappedValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapValueCompareCanBeCalledDirectlyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedValueCompareConstObjectUsesLiveInputsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedExtractionCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedNodeHintedInsertionGuardRejected', 'TranslateTest.CoreV2OrderedMapValueCompareCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedMapValueCompareOtherMappedTypeGuardRejected'])
        self.assertEqual(evidence['original_test_names'], 8804)
        self.assertEqual(evidence['selected_count'], 16)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-set-extract-keeps-detached-node-until-handle-destruction', output)
        self.assertIn(b'ordered-node-hinted-insertion-guard', output)

    def test_ordered_node_and_value_compare_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_node_and_value_compare_history(self.ordered_node_and_value_compare_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_node_and_value_compare_registered, self.ordered_node_and_value_compare_suffix + b' ')


    def test_ordered_hints_allocator_ownership_retains_published_bodies(self):
        self.assertEqual(self.ordered_hints_allocator_ownership_original + self.ordered_hints_allocator_ownership_suffix, self.ordered_hints_allocator_ownership_registered)
        self.assertEqual(self.adapter.restore_ordered_hints_allocator_ownership_history(self.ordered_hints_allocator_ownership_original),
                         self.ordered_node_and_value_compare_original)
        output, evidence = self.extractor.extract(self.ordered_hints_allocator_ownership_original, ['TranslateTest.CoreV2OrderedSetHintedNodeConflictLeavesTheHandleRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapHintedNodeUsesConstCursorConversionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetHintedNodePrecedesAnEquivalentCursorRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapHintedFallbackPreservesEquivalentOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetHintedValueInsertionAndEmplacementRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetHintedValueInsertsBeforeEquivalentCursorRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapHintedTryEmplaceAndInsertOrAssignRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapHintedPairAndValuePreserveOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetAllocatorCopyEvaluatesAllocatorAndOwnsNewNodesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetAllocatorMoveTransfersExistingAddressesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAllocatorMoveKeepsPayloadAndNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapAllocatorCopyAndMoveRetainEquivalentSequenceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedHintedNodeCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedMapHintedEmplaceConvertingValueGuardRejected', 'TranslateTest.CoreV2OrderedSetHintedInsertCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedAllocatorExtendedConstructorCustomCompareGuardRejected'])
        self.assertEqual(evidence['original_test_names'], 8820)
        self.assertEqual(evidence['selected_count'], 16)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-multimap-hinted-fallback-preserves-equivalent-order', output)
        self.assertIn(b'ordered-hinted-node-custom-policy-guard', output)

    def test_ordered_hints_allocator_ownership_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_hints_allocator_ownership_history(self.ordered_hints_allocator_ownership_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_hints_allocator_ownership_registered, self.ordered_hints_allocator_ownership_suffix + b' ')


    def test_ordered_reverse_retains_published_bodies(self):
        self.assertEqual(self.ordered_reverse_original + self.ordered_reverse_suffix, self.ordered_reverse_registered)
        self.assertEqual(self.adapter.restore_ordered_reverse_history(self.ordered_reverse_original),
                         self.ordered_hints_allocator_ownership_original)
        output, evidence = self.extractor.extract(self.ordered_reverse_original, ['TranslateTest.CoreV2OrderedSetReverseEndpointsAndPostfixStepRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetConstReverseTraversalCountsEquivalentsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapReverseArrowMutatesSelectedNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapReverseFactoryAndConstEndpointRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetReverseRangeUsesHalfOpenEndpointsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetReverseRangeInsertionKeepsEquivalentEntriesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapReverseRangeRetainsExistingMappedValuesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapReverseRangePresentsEquivalentsInReverseOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapReverseCursorConvertsToConstRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapMixedReverseCursorsComparePositionsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapAssignsMutableReversePositionToConstRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapCopiesConstReverseEndFromMutableRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedReverseCursorCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedMapReverseCursorDifferentMappedStorageGuardRejected', 'TranslateTest.CoreV2OrderedReverseRangeCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedMappedReverseConversionCustomOrderGuardRejected', 'TranslateTest.CoreV2OrderedMappedReverseConversionOtherMappedStorageGuardRejected'])
        self.assertEqual(evidence['original_test_names'], 8837)
        self.assertEqual(evidence['selected_count'], 17)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-set-reverse-range-uses-half-open-endpoints', output)
        self.assertIn(b'ordered-reverse-range-custom-policy-guard', output)

    def test_ordered_reverse_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_reverse_history(self.ordered_reverse_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_reverse_registered, self.ordered_reverse_suffix + b' ')


    def test_ordered_value_input_retains_published_bodies(self):
        self.assertEqual(self.ordered_value_input_original + self.ordered_value_input_suffix, self.ordered_value_input_registered)
        self.assertEqual(self.adapter.restore_ordered_value_input_history(self.ordered_value_input_original),
                         self.ordered_reverse_original)
        output, evidence = self.extractor.extract(self.ordered_value_input_original, ['TranslateTest.CoreV2OrderedMapConstructsFromMutablePairPointersRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapInsertsConstPairPointerSubrangeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapConstructsFromConstPairPointersRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapInsertsMutablePairPointersRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetEmplacementConvertsIntegerKeyBeforeLookupRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapEmplacementConvertsKeysAndMappedIntegersRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapPointerRangeDifferentMappedLeafGuardRejected', 'TranslateTest.CoreV2OrderedEmplacementUserDefinedKeyConversionGuardRejected'])
        self.assertEqual(evidence['original_test_names'], 8845)
        self.assertEqual(evidence['selected_count'], 8)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-map-constructs-from-mutable-pair-pointers', output)
        self.assertIn(b'ordered-map-pointer-range-different-mapped-leaf-guard', output)

    def test_ordered_value_input_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_value_input_history(self.ordered_value_input_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_value_input_registered, self.ordered_value_input_suffix + b' ')


    def test_ordered_map_and_iteration_retains_published_bodies(self):
        self.assertEqual(self.ordered_map_and_iteration_original + self.ordered_map_and_iteration_suffix, self.ordered_map_and_iteration_registered)
        self.assertEqual(self.adapter.restore_ordered_map_and_iteration_history(self.ordered_map_and_iteration_original),
                         self.ordered_value_input_original)
        output, evidence = self.extractor.extract(self.ordered_map_and_iteration_original, ['TranslateTest.CoreV2OrderedMapDefaultEmplacementInitializesBothFieldsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapEmplacementCopiesExactIntegerPairRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapEmplacementOfExactPairsKeepsOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapDefaultEmplacementPreservesExistingZeroKeyRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapPiecewiseEmplacementReadsLiveReferenceElementsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapPiecewiseEmptyKeyAndValueTuplesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapPiecewiseInsertsEquivalentValuesInOrderRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapPiecewiseConstructsBeforeUniqueConflictRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedIteratorAdvanceWalksInBothDirectionsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapDistanceCountsConstIteratorsAndEmptyRangesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedIteratorNextAndPrevCopyTheirInputRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapNextRetainsEquivalentPositionRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedSetFreeBeginEndAndEmptyFollowOwnerRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapFreeBeginKeepsMappedMutabilityRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetFreeReverseAccessRetainsEquivalentValuesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapFreeConstReverseAccessUsesOwnerEndpointsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedReverseAdvanceMutatesOnlyTheSelectedCursorRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapReverseDistanceKeepsEquivalentPositionsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapReverseNextPrevPreserveSourceAndMappedReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultisetReverseAdvanceCanReachAndLeaveEndRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapHintedDefaultEmplacementPreservesDuplicateValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapHintedExactPairPreservesExistingMappedValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapHintedExactPairInsertsBeforeEquivalentHintRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapHintedPiecewisePositionsValueAndKeepsEquivalentsRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedWideMapSubscriptKeepsExistingValueAndZeroInitializesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedWideMultimapPiecewiseAndHintedPairKeep64BitValuesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedWideMapPointerRangeCopyAndNodeHandlePreserveLayoutRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedWideMapComparisonAndAssignmentUseFullSignedValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtReadsExistingConstMappedReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtProvesKeyInLaterInitializerEntryRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtMappedReferenceRetainsWideTypeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtDuplicateInitializerKeepsFirstValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapEmplacementPairWithDifferentMappedLeafGuardRejected', 'TranslateTest.CoreV2OrderedMapPairEmplacementCustomOrderGuardRejected', 'TranslateTest.CoreV2OrderedMapPiecewiseNarrowTupleLeafGuardRejected', 'TranslateTest.CoreV2OrderedMapPiecewiseCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedIteratorNavigationCustomComparatorGuardRejected', 'TranslateTest.CoreV2OrderedIteratorNavigationDifferentMappedLayoutGuardRejected', 'TranslateTest.CoreV2OrderedFreeAccessCustomComparatorGuardRejected', 'TranslateTest.CoreV2OrderedFreeAccessOtherMappedStorageGuardRejected', 'TranslateTest.CoreV2OrderedReverseNavigationCustomComparatorGuardRejected', 'TranslateTest.CoreV2OrderedReverseNavigationDifferentMappedLayoutGuardRejected', 'TranslateTest.CoreV2OrderedMapHintedPairDifferentMappedLeafGuardRejected', 'TranslateTest.CoreV2OrderedMapHintedPiecewiseCustomPolicyGuardRejected', 'TranslateTest.CoreV2OrderedWideMappedFloatingStorageGuardRejected', 'TranslateTest.CoreV2OrderedWideMapOtherPairLeafGuardRejected', 'TranslateTest.CoreV2OrderedMapAtMissingStaticKeyRemainsThrowingRejected', 'TranslateTest.CoreV2OrderedMapAtReferenceReceiverDoesNotProvePresenceRejected'])
        self.assertEqual(evidence['original_test_names'], 8893)
        self.assertEqual(evidence['selected_count'], 48)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-map-default-emplacement-initializes-both-fields', output)
        self.assertIn(b'ordered-map-emplacement-pair-with-different-mapped-leaf-guard', output)

    def test_ordered_map_and_iteration_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_map_and_iteration_history(self.ordered_map_and_iteration_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_map_and_iteration_registered, self.ordered_map_and_iteration_suffix + b' ')


    def test_ordered_map_scalar_and_checked_at_retains_published_bodies(self):
        self.assertEqual(self.ordered_map_scalar_and_checked_at_original + self.ordered_map_scalar_and_checked_at_suffix, self.ordered_map_scalar_and_checked_at_registered)
        self.assertEqual(self.adapter.restore_ordered_map_scalar_and_checked_at_history(self.ordered_map_scalar_and_checked_at_original),
                         self.ordered_map_and_iteration_original)
        output, evidence = self.extractor.extract(self.ordered_map_scalar_and_checked_at_original, ['TranslateTest.CoreV2OrderedMapLongLayoutFollowsTargetWidthRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapUnsignedLongLongComparesAboveSignedMaximumRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapUintNodeAndComparisonRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapUlongAndSizeTRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapUshortAndMapShortRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapBoolAndCharacterValuesRunAtBothOptimizations', 'TranslateTest.CoreV2ClimitsMatchesCxxIntegerLimitsRunAtBothOptimizations', 'TranslateTest.CoreV2ClimitsLongWidthFollowsTargetRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapLongStoresClimitsExtremesRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapWideUnsignedUsesClimitsMaximumRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtFindTernaryOnConstReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtIfReturnsMutableReferenceRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtIfPreservesWideMappedValueRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapAtMutableTernaryDoesNotInsertRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapFloatSubscriptCopyAndNodeRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMultimapDoublePiecewiseAndEqualKeysRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapDoubleComparisonAdvancesPastUnorderedPairTypedInputRunAtBothOptimizations', 'TranslateTest.CoreV2OrderedMapConstLongMappedStorageGuardRejected', 'TranslateTest.CoreV2OrderedMapUnsignedLongLongDifferentPairLeafGuardRejected', 'TranslateTest.CoreV2OrderedMapQualifiedUintValueGuardRejected', 'TranslateTest.CoreV2OrderedMapEnumMappedValueGuardRejected', 'TranslateTest.CoreV2ClimitsQuotedSourceIncludeIsRejectedRejected', 'TranslateTest.CoreV2LimitsResourceHeaderDirectSourceIncludeIsRejectedRejected', 'TranslateTest.CoreV2OrderedMapAtGuardDifferentKeyRejected', 'TranslateTest.CoreV2OrderedMapAtGuardDifferentOwnerRejected', 'TranslateTest.CoreV2OrderedMapDoubleComparisonAdvancesPastUnorderedPairRejected', 'TranslateTest.CoreV2OrderedMapLongDoubleStorageGuardRejected', 'TranslateTest.CoreV2OrderedMapConstDoubleStorageGuardRejected'])
        self.assertEqual(evidence['original_test_names'], 8921)
        self.assertEqual(evidence['selected_count'], 28)
        self.assertTrue(evidence['all_non_test_source_retained'])
        self.assertIn(b'ordered-map-uint-node-and-comparison', output)
        self.assertIn(b'ordered-map-qualified-uint-value-guard', output)

    def test_ordered_map_scalar_and_checked_at_rejects_an_edited_source_or_suffix(self):
        with self.assertRaises(AssertionError):
            self.adapter.restore_ordered_map_scalar_and_checked_at_history(self.ordered_map_scalar_and_checked_at_registered + b' ')
        with self.assertRaises(AssertionError):
            self.adapter.restore_original_gtests(self.ordered_map_scalar_and_checked_at_registered, self.ordered_map_scalar_and_checked_at_suffix + b' ')


if __name__ == '__main__':
    unittest.main()

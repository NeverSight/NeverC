#!/usr/bin/env python3
"""Regression tests for native multi-result intrinsic signature encoding."""

import importlib.util
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "native_intrinsics", Path(__file__).with_name("sync-x86-native-intrinsics.py"))
NATIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NATIVE)


class NativeSignaturesTest(unittest.TestCase):
    def setUp(self):
        self.impl = NATIVE.SORTER.INC_PATH.read_text()

    def test_compact_signatures_do_not_change_the_long_table(self):
        encoding, updated = NATIVE.encode_signature(("I8", "I64", "PTR"), self.impl)
        self.assertEqual(NATIVE.encoding_value(encoding), 0xE52)
        self.assertEqual(updated, self.impl)

    def test_register_read_uses_the_exact_result_pair_and_parameters(self):
        encoding, updated = NATIVE.encode_signature(
            ("STRUCT2", "I64", "I8", "I64", "I64"), self.impl)
        self.assertEqual(updated, self.impl)
        self.assertTrue(NATIVE.encoding_value(encoding) & (1 << 31))
        offset = NATIVE.encoding_value(encoding) & ((1 << 31) - 1)
        self.assertIn(f"/* {offset} */ 21, 5, 2, 5, 5, 0,", self.impl)

    def test_new_long_signatures_append_without_retargeting_old_offsets(self):
        signature = ("STRUCT2", "I16", "I64", "I8", "I16", "I32")
        encoding, updated = NATIVE.encode_signature(signature, self.impl)
        self.assertNotEqual(updated, self.impl)
        self.assertEqual(NATIVE.encode_signature(signature, updated), (encoding, updated))
        start = self.impl.index("static const unsigned char IIT_LongEncodingTable")
        sentinel = self.impl.index("\n  255\n", start)
        self.assertEqual(updated[:sentinel], self.impl[:sentinel])
        self.assertEqual(updated[updated.index("\n  255\n", start):], self.impl[sentinel:])

    def test_checked_in_native_schema_is_synchronized(self):
        NATIVE.synchronize(False)

    def test_address_controls_have_distinct_immediate_positions(self):
        first, initial = NATIVE.immediate_arguments_attribute(self.impl, (1, 2))
        second, updated = NATIVE.immediate_arguments_attribute(initial, (2, 3))
        self.assertNotEqual(first, second)
        self.assertEqual(updated, self.impl)
        for indices, recipe in (((1, 2), first), ((2, 3), second)):
            body = updated.split(f"    case {recipe}: {{\n", 1)[1].split("    }", 1)[0]
            for position, argument in enumerate(indices):
                self.assertIn(f"AS[{position}] = {{{argument + 1}, getIntrinsicArgAttributeSet(C, 0)}}", body)
            self.assertIn("getIntrinsicFnAttributeSet(C, 7)", body)

    def test_new_immediate_recipes_do_not_reuse_an_existing_recipe_id(self):
        first, initial = NATIVE.immediate_arguments_attribute(self.impl, (4, 5))
        second, updated = NATIVE.immediate_arguments_attribute(initial, (5, 6))
        self.assertNotEqual(first, second)
        self.assertEqual(NATIVE.immediate_arguments_attribute(updated, (4, 5)),
                         (first, updated))


if __name__ == "__main__":
    unittest.main()

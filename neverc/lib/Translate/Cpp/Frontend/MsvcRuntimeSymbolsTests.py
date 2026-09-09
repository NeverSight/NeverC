#!/usr/bin/env python3
"""Positive MSVC archive observations and fail-closed identity mutations."""

import json
from pathlib import Path
import unittest

from MsvcRuntimeSymbols import (msvc_delete_weak_reference,
                                msvc_runtime_definition)


class MsvcRuntimeSymbolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = json.loads(Path(__file__).with_name(
            "MsvcRuntimeSymbolsFixture.json").read_text(encoding="utf-8"))

    def test_complete_observed_msvc_archive_inventory(self):
        self.assertEqual(len(self.rows), 229)
        self.assertEqual(sum(kinds == ["w"] for _, kinds, _ in self.rows), 1)
        for name, kinds, decoded in self.rows:
            with self.subTest(name=name):
                self.assertEqual(msvc_runtime_definition(name, decoded, kinds),
                                 kinds != ["w"])
                self.assertEqual(msvc_delete_weak_reference(name, decoded, kinds),
                                 kinds == ["w"])

    def test_wrong_kinds_mixed_inventory_and_missing_definitions_fail(self):
        for name, kinds, decoded in self.rows:
            predicate = (msvc_delete_weak_reference if kinds == ["w"]
                         else msvc_runtime_definition)
            for wrong in ([], ["U"], ["T"], ["R"], ["B"], ["C"], ["D"],
                          ["w"], ["W"], ["v"], ["V"], kinds + ["U"]):
                if wrong == kinds:
                    continue
                with self.subTest(name=name, kinds=wrong):
                    self.assertFalse(predicate(name, decoded, wrong))

    def test_complete_names_and_declarations_are_required(self):
        for name, kinds, decoded in self.rows:
            predicate = (msvc_delete_weak_reference if kinds == ["w"]
                         else msvc_runtime_definition)
            for suffix in ("extra", " ", "\n", "\x00", "@"):
                with self.subTest(name=name, suffix=repr(suffix)):
                    self.assertFalse(predicate(name + suffix, decoded, kinds))
                    self.assertFalse(predicate(name, decoded + suffix, kinds))
            self.assertFalse(predicate(name, "", kinds))
            self.assertFalse(predicate(name, "void __cdecl llvm::unexpected(void)", kinds))

    def test_other_stdext_entities_and_allocator_names_are_not_exempted(self):
        for name, decoded in (
            ("?call@other@stdext@@YAXXZ", "void __cdecl stdext::other::call(void)"),
            ("?__global_delete@@YAXPEAX@Z", "void __cdecl __global_delete(void *)"),
            ("?__empty_global_delete@@YAXPEAX@Host@@@Z",
             "void __cdecl Host::__empty_global_delete(void *)"),
            ("_CTA3?AVPrivateError@neverc_cpp_llvm@@", "_CTA3?AVPrivateError@neverc_cpp_llvm@@"),
            ("strdup", "strdup"), ("LLVMCreateMessage", "LLVMCreateMessage"),
            ("printf_wrong_abi", "printf_wrong_abi"),
        ):
            for kinds in (["T"], ["R"], ["w"]):
                self.assertFalse(msvc_runtime_definition(name, decoded, kinds))
                self.assertFalse(msvc_delete_weak_reference(name, decoded, kinds))

    def test_encoded_constant_widths_and_hex_alphabet(self):
        for name in ("__real@" + "a" * 8, "__real@" + "0" * 16,
                     "__xmm@" + "f" * 32, "__ymm@" + "1" * 64,
                     "_GUID_12345678_1234_5678_abcd_123456789abc"):
            self.assertTrue(msvc_runtime_definition(name, name, {"R"}))
            for changed in (name[:-1], name + "0", name + "_helper",
                            name[:-1] + "g", name.upper()):
                self.assertFalse(msvc_runtime_definition(changed, changed, {"R"}))

    def test_demangler_comma_spacing_does_not_permit_other_whitespace(self):
        for name, kinds, decoded in self.rows:
            if ", " not in decoded:
                continue
            predicate = (msvc_delete_weak_reference if kinds == ["w"]
                         else msvc_runtime_definition)
            self.assertTrue(predicate(name, decoded.replace(", ", ","), kinds))
            for separator in (",  ", ",\t", ",\n", ",\x00"):
                self.assertFalse(predicate(name, decoded.replace(", ", separator), kinds))


if __name__ == "__main__":
    unittest.main()

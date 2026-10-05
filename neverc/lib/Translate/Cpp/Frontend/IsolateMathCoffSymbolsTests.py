#!/usr/bin/env python3
"""Check math isolation rejects altered code, linkage and source call sites."""

import ast
from dataclasses import replace
import hashlib
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

import IsolateMathCoffSymbols as math
from RewriteSetupCoffSymbols import CoffObject, CoffSection, CoffSymbol


class MathIsolationTests(unittest.TestCase):
    def snapshots(self):
        old, new = next(iter(math.MATH_RENAMES.items()))
        section = CoffSection(".text", 0, 0, 1, 0x60000020,
                              hashlib.sha256(b"\xc3").hexdigest(), ())
        symbol = CoffSymbol(0, old, 0, 1, 0x20, 2, (), 100)
        obj = CoffObject(0x8664, 0, 0, False, (symbol,), (section,), "before", 200, ())
        original = SimpleNamespace(machine=0x8664, members=[("math.obj", obj)],
                                   member_offsets=(100,),
                                   first_index=((old, 100, 50),),
                                   second_index=((old, 100, 70),))
        changed = SimpleNamespace(machine=0x8664,
                                  members=[("math.obj", replace(
                                      obj, symbols=(replace(symbol, name=new, name_at=120),)))],
                                  member_offsets=(150,),
                                  first_index=((new, 150, 50),),
                                  second_index=((new, 150, 80),))
        return original, changed, {old: new}

    def test_preserves_code_and_resolves_both_archive_indices(self):
        old, new, mapping = self.snapshots()
        self.assertEqual(math.verify_pair(old, new, mapping),
                         {next(iter(mapping)): 1})

    def test_rejects_changed_code_or_relocations(self):
        for field in ("contents", "relocations", "flags"):
            old, new, mapping = self.snapshots()
            name, obj = new.members[0]
            section = obj.sections[0]
            value = {"contents": "other", "relocations": ((0, 0, 1),),
                     "flags": section.flags ^ 0x20}[field]
            new.members[0] = name, replace(obj, sections=(replace(section, **{field: value}),))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "section"):
                math.verify_pair(old, new, mapping)

    def test_rejects_changed_symbol_and_auxiliary_identity(self):
        for field in ("index", "value", "section", "kind", "storage", "aux"):
            old, new, mapping = self.snapshots()
            name, obj = new.members[0]
            symbol = obj.symbols[0]
            value = (b"changed",) if field == "aux" else getattr(symbol, field) + 1
            new.members[0] = name, replace(obj, symbols=(replace(symbol, **{field: value}),))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "identity"):
                math.verify_pair(old, new, mapping)

    def test_rejects_archive_representative_or_multiplicity_change(self):
        for field in ("first_index", "second_index"):
            old, new, mapping = self.snapshots()
            setattr(new, field, getattr(new, field) * 2)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "multiplicity"):
                math.verify_pair(old, new, mapping)

    def test_rejects_member_order_and_object_format_changes(self):
        old, new, mapping = self.snapshots()
        _, obj = new.members[0]
        new.members[0] = "another.obj", obj
        with self.assertRaisesRegex(ValueError, "name or order"):
            math.verify_pair(old, new, mapping)
        new.members[0] = "math.obj", replace(obj, bigobj=True)
        with self.assertRaisesRegex(ValueError, "header"):
            math.verify_pair(old, new, mapping)

    def test_uninitialized_storage_only_accepts_zero_serialization(self):
        empty = hashlib.sha256(b"").hexdigest()
        section = CoffSection(".bss", 0, 0, 4, 0xC0000080, empty, ())
        zero = replace(section, contents=hashlib.sha256(bytes(4)).hexdigest())
        self.assertTrue(math.equivalent_section(section, zero))
        self.assertFalse(math.equivalent_section(section, replace(zero, contents="other")))
        initialized = replace(section, flags=0xC0000040)
        self.assertFalse(math.equivalent_section(initialized, replace(zero, flags=initialized.flags)))

    def test_rejects_unknown_sdk_instantiation_and_partial_previous_isolation(self):
        old, new = next(iter(math.MATH_RENAMES.items()))
        self.assertEqual(math.checked_names([old, "other"]), {old: new})
        for names in (["??$pow@NN$0A@@@YANNN@Z"], [old, new], [new]):
            with self.subTest(names=names), self.assertRaises(ValueError):
                math.checked_names(names)


class MathSourceTests(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).with_name("IsolateSymbols.py")
        function = next(node for node in ast.parse(path.read_text()).body
                        if isinstance(node, ast.FunctionDef) and node.name == "isolate_math_calls")
        namespace = {}
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(path), "exec"), namespace)
        self.isolate = namespace["isolate_math_calls"]
        temporary = tempfile.TemporaryDirectory(prefix="neverc-math-source-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sources = {
            "llvm/lib/Support/Signals.cpp": "std::log10(Depth);\n",
            "llvm/lib/Support/APFixedPoint.cpp": (
                "std::pow(2, Sema.getLsbWeight());\n"
                "std::pow(2, -DstFXSema.getLsbWeight());\n"
                "std::pow(2, DstFXSema.getLsbWeight());\n"),
            "llvm/lib/Analysis/ConstantFolding.cpp": (
                "std::pow(Op1V.convertToFloat(), Exp);\n"
                "std::pow(Op1V.convertToDouble(), Exp);\n"),
        }
        for name, contents in self.sources.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)

    def test_coff_keeps_all_original_calls_and_is_idempotent(self):
        self.isolate(self.root, True)
        self.isolate(self.root, True)
        for name, contents in self.sources.items():
            self.assertEqual((self.root / name).read_text(), contents)

    def test_existing_private_checkout_can_restore_original_coff_calls(self):
        self.isolate(self.root)
        self.assertIn("static_cast<double>",
                      (self.root / "llvm/lib/Support/Signals.cpp").read_text())
        self.isolate(self.root, True)
        for name, contents in self.sources.items():
            self.assertEqual((self.root / name).read_text(), contents)

    def test_partial_or_duplicate_calls_fail_before_any_math_write(self):
        fixed = self.root / "llvm/lib/Support/APFixedPoint.cpp"
        original = fixed.read_text()
        for bad in (original.replace("std::pow(2, Sema.getLsbWeight())", "0.0"),
                    original + "std::pow(2, Sema.getLsbWeight());\n",
                    original.replace("std::pow(2, Sema.getLsbWeight())",
                                     "std::pow(2.0, static_cast<double>(Sema.getLsbWeight()))")):
            fixed.write_text(bad)
            before = {name: (self.root / name).read_bytes() for name in self.sources}
            with self.subTest(source=bad), self.assertRaises(SystemExit):
                self.isolate(self.root, True)
            self.assertEqual(before, {name: (self.root / name).read_bytes() for name in self.sources})


if __name__ == "__main__":
    unittest.main()

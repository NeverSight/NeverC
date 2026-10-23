#!/usr/bin/env python3
"""Check system pseudo synchronization, append-only identities and drift rejection."""

import importlib.util
import re
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "svm", Path(__file__).with_name("sync-x86-system-address-instructions.py"))
SVM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SVM)


class SVMAddressTablesTest(unittest.TestCase):
    def test_checked_in_tables_are_synchronized(self):
        for path, expected in SVM.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_existing_opcodes_keep_their_identities(self):
        text = (SVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        ids = SVM.TABLES.enums(text)
        for name, value in (("INVLPGA64", 1442), ("VMLOAD64", 10225),
                            ("VMSAVE64", 10809), ("LKGS16m", 18490)):
            self.assertEqual(ids[name], value)
        for name, _, _, _ in SVM.INSTRUCTIONS:
            self.assertGreater(ids[name], ids["LKGS16m"])
            fields = SVM.TABLES.descriptor(text, name)
            self.assertIn("MCID::Pseudo", fields[9])
            self.assertEqual(fields[9].split(", ")[1], "0x0ULL")

    def test_lost_pseudo_property_is_rejected(self):
        text = (SVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        corrupted = re.sub(r"\|\(1ULL<<MCID::Pseudo\)(?=, 0x0ULL \},  // Inst #\d+ = VMLOAD64_ADDR32)", "", text)
        self.assertNotEqual(corrupted, text)
        with self.assertRaisesRegex(ValueError, "stale.*instruction"):
            SVM.synchronize_table(corrupted)

    def test_explicit_memory_preserves_operand_classes_memory_and_flags(self):
        text = (SVM.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for prototype in (*SVM.VMX, *SVM.DESCRIPTOR):
            original = SVM.TABLES.descriptor(text, prototype)
            pseudo = SVM.TABLES.descriptor(text, prototype + "_ADDR32")
            self.assertEqual(pseudo[1:9], original[1:9], prototype)
            self.assertEqual(pseudo[9].split(", ")[0],
                             original[9].split(", ")[0] + "|(1ULL<<MCID::Pseudo)")


if __name__ == "__main__":
    unittest.main()

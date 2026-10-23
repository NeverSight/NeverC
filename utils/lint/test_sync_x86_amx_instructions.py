#!/usr/bin/env python3
"""Regression checks for native AMX table synchronization."""

import importlib.util
import re
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "amx", Path(__file__).with_name("sync-x86-amx-instructions.py"))
AMX = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AMX)


class AMXTablesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.table = (AMX.TARGET / "X86GenInstrInfo.inc").read_text()

    def test_existing_instruction_numbers_remain_stable(self):
        ids = AMX.enums(self.table)
        self.assertEqual(ids["TDPBSSD"], 3537)
        self.assertEqual(ids["TILELOADD"], 3563)
        self.assertEqual(ids["WRITE_SYSREG64"], 18448)
        self.assertEqual(self.table, AMX.synchronize_instructions(self.table))

    def test_corrupt_opcode_encoding_is_rejected(self):
        bad = re.sub(r"(0x)[a-f0-9]+(ULL[^\n]+ = TDPBF8PS)",
                     r"\g<1>0\2", self.table, count=1)
        self.assertNotEqual(self.table, bad)
        with self.assertRaisesRegex(ValueError, "stale AMX instruction"):
            AMX.synchronize_instructions(bad)

    def test_both_writers_cover_every_opcode(self):
        ids = AMX.enums(self.table)
        for filename in ("X86GenAsmWriter.inc", "X86GenAsmWriter1.inc"):
            text = (AMX.TARGET / filename).read_text()
            self.assertEqual(text, AMX.synchronize_writer(text, ids))
            for declaration in re.findall(r"  static const uint\d+_t OpInfo\d\[\] = \{", text):
                _, _, content = AMX.array(text, declaration)
                self.assertEqual(len(re.findall(r"\d+U,", content)), len(ids))

    def test_assembler_and_feature_tables_are_idempotent(self):
        for filename, synchronize in (
                ("X86GenAsmMatcher.inc", AMX.synchronize_matcher),
                ("X86GenSubtargetInfo.inc", AMX.synchronize_features),
                ("X86GenEVEX2VEXTables.inc", AMX.synchronize_compression)):
            text = (AMX.TARGET / filename).read_text()
            self.assertEqual(text, synchronize(text))


if __name__ == "__main__":
    unittest.main()

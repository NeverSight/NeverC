#!/usr/bin/env python3
"""Check LKGS encoding, table synchronization and unchanged existing opcodes."""

import importlib.util
import re
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "lkgs", Path(__file__).with_name("sync-x86-lkgs-instructions.py"))
LKGS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LKGS)


class LKGSTablesTest(unittest.TestCase):
    def test_checked_in_tables_are_synchronized(self):
        for path, expected in LKGS.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_append_keeps_old_opcodes_and_exact_native_encoding(self):
        table = (LKGS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        ids = LKGS.TABLES.enums(table)
        self.assertEqual(ids["LLDT16r"], 1610)
        self.assertEqual(ids["WRITE_SYSREG64"], 18448)
        self.assertEqual(ids["TDPBF8PS"], 18449)
        for name, expected in (("LKGS16r", "0x3836ULL"), ("LKGS16m", "0x3826ULL")):
            fields = LKGS.TABLES.descriptor(table, name)
            self.assertEqual(fields[9].split(", ")[1], expected)
            self.assertIn("UnmodeledSideEffects", fields[9])
            self.assertIn("MayLoad", fields[9])  # Both forms read the descriptor.

    def test_corrupt_modrm_extension_is_rejected(self):
        table = (LKGS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        corrupted = re.sub(r"0x3836ULL(?= \},  // Inst #\d+ = LKGS16r)", "0x3832ULL", table)
        self.assertNotEqual(corrupted, table)
        with self.assertRaisesRegex(ValueError, "stale.*instruction"):
            LKGS.TABLES.synchronize_instructions(corrupted, LKGS.INSTRUCTIONS, {})


if __name__ == "__main__":
    unittest.main()

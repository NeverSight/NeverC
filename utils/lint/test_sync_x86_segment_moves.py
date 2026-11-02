#!/usr/bin/env python3
"""Check the segment write carriers and stable generated instruction tables."""

import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "segments", Path(__file__).with_name("sync-x86-segment-moves.py"))
SEGMENTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SEGMENTS)


class SegmentMovesTest(unittest.TestCase):
    def test_checked_in_tables_and_idempotence(self):
        for path, text in SEGMENTS.synchronize().items():
            self.assertEqual(text, path.read_text(), path.name)
        text = (SEGMENTS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        self.assertEqual(SEGMENTS.synchronize_table(text), text)

    def test_custom_insertion_preserves_flags_and_descriptor_effects(self):
        text = (SEGMENTS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, kinds in SEGMENTS.SCHEMA:
            fields = SEGMENTS.TABLES.descriptor(text, name)
            self.assertEqual(fields[2:4], ["0", "0"])
            self.assertEqual(fields[5:7], ["0", "0"])
            self.assertEqual(int(fields[1]), 6 if kinds[-1] == "i16mem" else 2)
            for flag in ("UsesCustomInserter", "UnmodeledSideEffects", "MayLoad", "MayStore"):
                self.assertIn("MCID::" + flag, fields[9])
        ids = SEGMENTS.TABLES.enums(text)
        self.assertEqual(ids["WRITE_SYSREG64"], 18448)


if __name__ == "__main__":
    unittest.main()

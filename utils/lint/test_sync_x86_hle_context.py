#!/usr/bin/env python3
"""Verify that the HLE pseudo cannot lose its implicit state or memory effects."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "hle", Path(__file__).with_name("sync-x86-hle-context.py"))
HLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HLE)


class HLEContextTableTest(unittest.TestCase):
    def test_native_effects(self):
        for path, expected in HLE.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (HLE.CONTEXT.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        fields = HLE.CONTEXT.TABLES.descriptor(text, "HLE_CONTEXT64")
        self.assertEqual(fields[1:4], ["2", "0", "0"])
        self.assertEqual(fields[5:7], ["3", "5"])
        offset = fields[7].removeprefix("X86ImpOpBase + ")
        sequence = re.search(rf"/\* {offset} \*/ (X86::[^\n]+)", text).group(1)
        self.assertEqual(re.findall(r"X86::(\w+)", sequence),
                         ["R9", "R10", "R11", "RAX", "RCX", "RDX", "RBX", "EFLAGS"])
        for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            self.assertIn(f"MCID::{flag}", fields[9])
        self.assertNotIn("MCID::Call", fields[9])

    def test_missing_effect_is_rejected(self):
        text = (HLE.CONTEXT.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        line = re.search(r"^.*// Inst #\d+ = HLE_CONTEXT64$", text, re.M).group(0)
        for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            corrupted = line.replace(f"|(1ULL<<MCID::{flag})", "")
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                HLE.CONTEXT.synchronize_table(text.replace(line, corrupted), HLE.SCHEMA)


if __name__ == "__main__":
    unittest.main()

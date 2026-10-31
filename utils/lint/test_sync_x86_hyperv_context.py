#!/usr/bin/env python3
"""Protect the full output bank and real-call frame contract."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "hyperv", Path(__file__).with_name("sync-x86-hyperv-context.py"))
HYPERV = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HYPERV)


class HyperVContextTableTest(unittest.TestCase):
    def test_complete_register_effects_and_call_frame(self):
        for path, expected in HYPERV.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (HYPERV.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        fields = HYPERV.TABLES.descriptor(text, "HYPERV_CALL64")
        self.assertEqual(fields[1:4], ["1", "0", "0"])
        self.assertEqual(fields[5:7], ["4", "11"])
        offset = fields[7].removeprefix("X86ImpOpBase + ")
        sequence = re.search(rf"/\* {offset} \*/ (X86::[^\n]+)", text).group(1)
        self.assertEqual(re.findall(r"X86::(\w+)", sequence),
                         ["R9", "R10", "R11", "RSP", "RAX", "RCX", "RDX", "R8",
                          "XMM0", "XMM1", "XMM2", "XMM3", "XMM4", "XMM5", "EFLAGS"])
        for flag in ("Call", "MayLoad", "MayStore", "UnmodeledSideEffects"):
            self.assertIn(f"MCID::{flag}", fields[9])

    def test_removed_frame_or_effect_is_rejected(self):
        text = (HYPERV.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        line = re.search(r"^.*// Inst #\d+ = HYPERV_CALL64$", text, re.M).group(0)
        for flag in ("Call", "MayLoad", "MayStore"):
            corrupted = line.replace(f"|(1ULL<<MCID::{flag})", "")
            self.assertNotEqual(line, corrupted)
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                HYPERV.synchronize_table(text.replace(line, corrupted))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Retain one immediate, EAX completion and conservative RTM pair effects."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "rtm", Path(__file__).with_name("sync-x86-rtm-abort-pair.py"))
RTM = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RTM)


class RTMAbortPairTableTest(unittest.TestCase):
    def test_native_effects(self):
        for path, expected in RTM.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (RTM.CONTEXT.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        fields = RTM.CONTEXT.TABLES.descriptor(text, "RTM_ABORT_PAIR")
        self.assertEqual(fields[1:4], ["1", "0", "0"])
        self.assertEqual(fields[5:7], ["0", "2"])
        offset = fields[7].removeprefix("X86ImpOpBase + ")
        sequence = re.search(rf"/\* {offset} \*/ (X86::[^\n]+)", text).group(1)
        self.assertEqual(re.findall(r"X86::(\w+)", sequence)[:2],
                         ["EAX", "EFLAGS"])
        for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            self.assertIn(f"MCID::{flag}", fields[9])
        self.assertNotIn("MCID::Call", fields[9])
        self.assertNotIn("MCID::Terminator", fields[9])

    def test_missing_effect_is_rejected(self):
        text = (RTM.CONTEXT.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        line = re.search(r"^.*// Inst #\d+ = RTM_ABORT_PAIR$", text, re.M).group(0)
        for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            corrupted = line.replace(f"|(1ULL<<MCID::{flag})", "")
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                RTM.CONTEXT.synchronize_table(text.replace(line, corrupted),
                                              RTM.SCHEMA, ("i32imm",))


if __name__ == "__main__":
    unittest.main()

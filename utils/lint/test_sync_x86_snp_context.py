#!/usr/bin/env python3
"""Check native SNP effects, encodings, and append-only table synchronization."""

import importlib.util
from pathlib import Path
import re
import unittest

SPEC = importlib.util.spec_from_file_location(
    "snp", Path(__file__).with_name("sync-x86-snp-context.py"))
SNP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SNP)


class SNPContextTableTest(unittest.TestCase):
    def test_effects_and_encodings(self):
        for path, expected in SNP.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (SNP.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name, expected in {"RMPREAD": 0x8000387d,
                               "RMPOPT": 0x8000387c,
                               "RMPCHKD": 0x8000307c}.items():
            fields = SNP.TABLES.descriptor(text, name)
            self.assertEqual(int(fields[9].split(", ")[1].removesuffix("ULL"), 16), expected)
        fields = SNP.TABLES.descriptor(text, "SNP_CONTEXT64")
        self.assertEqual(fields[1:4], ["2", "0", "0"])
        self.assertEqual(fields[5:7], ["2", "4"])
        offset = fields[7].removeprefix("X86ImpOpBase + ")
        sequence = re.search(rf"/\* {offset} \*/ (X86::[^\n]+)", text).group(1)
        self.assertEqual(re.findall(r"X86::(\w+)", sequence),
                         ["R10", "R11", "RAX", "RCX", "RDX", "EFLAGS"])
        for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            self.assertIn(f"MCID::{flag}", fields[9])
        self.assertNotIn("MCID::Call", fields[9])

    def test_missing_effect_is_rejected(self):
        text = (SNP.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        line = re.search(r"^.*// Inst #\d+ = SNP_CONTEXT64$", text, re.M).group(0)
        for flag in ("MayLoad", "MayStore", "UnmodeledSideEffects"):
            corrupted = line.replace(f"|(1ULL<<MCID::{flag})", "")
            with self.assertRaisesRegex(ValueError, "stale.*instruction"):
                SNP.synchronize_table(text.replace(line, corrupted))


if __name__ == "__main__":
    unittest.main()

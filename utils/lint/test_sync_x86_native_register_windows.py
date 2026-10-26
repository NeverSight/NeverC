#!/usr/bin/env python3
"""Check the indivisible native register-window machine contracts."""

import importlib.util
import re
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "windows", Path(__file__).with_name("sync-x86-native-register-windows.py"))
WINDOWS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WINDOWS)


class NativeRegisterWindowTest(unittest.TestCase):
    def test_generated_tables_match_authority(self):
        for path, expected in WINDOWS.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_carrier_is_tied_and_excludes_the_reserved_base(self):
        text = (WINDOWS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        ids = WINDOWS.TABLES.enums(text)
        for i, (name, _, _, _) in enumerate(WINDOWS.INSTRUCTIONS):
            self.assertEqual(ids[name], 18573 + i)
            fields = WINDOWS.TABLES.descriptor(text, name)
            self.assertEqual(fields[1:3], ["2", "1"])
            operands = re.search(r"/\* " + fields[8] + r" \*/ ([^\n]+)", text)[1]
            self.assertEqual(operands.count("X86::GR64_TCW64RegClassID"), 2)
            self.assertIn("MCOI_TIED_TO(0)", operands)
            self.assertIn("MCID::Pseudo", fields[9])
            self.assertNotIn("MCID::MoveReg", fields[9])
            self.assertIn("MCID::UnmodeledSideEffects", fields[9])
        self.assertEqual(ids["NATIVE_ADDRESS64"], 18572)


if __name__ == "__main__":
    unittest.main()

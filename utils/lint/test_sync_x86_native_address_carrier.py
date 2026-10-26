#!/usr/bin/env python3
"""Protect carrier constraints and existing plugin opcode identities."""

import importlib.util
import re
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "carrier", Path(__file__).with_name("sync-x86-native-address-carrier.py"))
CARRIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CARRIER)


class NativeAddressCarrierTest(unittest.TestCase):
    def test_checked_in_tables_match_authority(self):
        for path, expected in CARRIER.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_carrier_is_not_an_optimizable_copy(self):
        text = (CARRIER.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        fields = CARRIER.TABLES.descriptor(text, "NATIVE_ADDRESS64")
        self.assertEqual(fields[1:3], ["2", "1"])
        self.assertEqual(fields[9], "0|(1ULL<<MCID::Pseudo), 0x0ULL")
        operands = re.search(r"/\* " + fields[8] + r" \*/ ([^\n]+)", text)[1]
        self.assertIn("X86::GR64_ABCDRegClassID", operands)
        self.assertIn("X86::GR64RegClassID", operands)
        ids = CARRIER.TABLES.enums(text)
        self.assertEqual(ids["AMX_MEMORY_NATIVE"], 18538)
        self.assertEqual(ids["NATIVE_ADDRESS64"], 18572)


if __name__ == "__main__":
    unittest.main()

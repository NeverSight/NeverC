#!/usr/bin/env python3
"""Protect the terminal stack-carrier contract and stable opcode identities."""

import importlib.util
import unittest
from pathlib import Path


SPEC = importlib.util.spec_from_file_location(
    "returns", Path(__file__).with_name("sync-x86-system-returns.py"))
RETURNS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RETURNS)


class SystemReturnsTest(unittest.TestCase):
    def test_checked_in_tables_match_authority(self):
        for path, expected in RETURNS.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_terminals_do_not_request_an_abi_epilogue(self):
        text = (RETURNS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        for name in ("SYSRET32_STACK", "SYSRET64_STACK", "SYSRET", "SYSRET64",
                     "SYSEXIT", "SYSEXIT64"):
            fields = RETURNS.TABLES.descriptor(text, name)
            for flag in ("Terminator", "Barrier", "UnmodeledSideEffects",
                         "MayLoad", "MayStore"):
                self.assertIn(f"MCID::{flag}", fields[9], name)
            self.assertNotIn("MCID::Return", fields[9], name)
        ids = RETURNS.TABLES.enums(text)
        self.assertEqual(ids["NATIVE_ADDRESS64"], 18572)


if __name__ == "__main__":
    unittest.main()

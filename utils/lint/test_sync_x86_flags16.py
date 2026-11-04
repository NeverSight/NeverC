#!/usr/bin/env python3
"""Word FLAGS descriptors must keep register widths and stable identities."""

import importlib.util
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "flags", Path(__file__).with_name("sync-x86-flags16.py"))
FLAGS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FLAGS)


class Flags16Test(unittest.TestCase):
    def test_installed_tables_are_idempotent(self):
        for path, expected in FLAGS.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))

    def test_word_pseudos_append_without_renumbering_old_opcodes(self):
        text = (FLAGS.TABLES.TARGET / "X86GenInstrInfo.inc").read_text()
        ids = FLAGS.TABLES.enums(text)
        self.assertEqual(ids["NATIVE_ADDRESS64"], 18572)
        self.assertEqual(ids["RDFLAGS16"], 18607)
        self.assertEqual(ids["WRFLAGS16"], 18608)
        for name, writes in (("RDFLAGS16", False), ("WRFLAGS16", True)):
            fields = FLAGS.TABLES.descriptor(text, name)
            self.assertEqual(fields[1:3], ["1", "0" if writes else "1"])
            self.assertEqual(fields[5:7], ["1", "3" if writes else "1"])
            for flag in ("Pseudo", "MayLoad", "MayStore", "UnmodeledSideEffects"):
                self.assertIn(f"MCID::{flag}", fields[9])
            self.assertNotIn("MCID::Terminator", fields[9])
            self.assertNotIn("MCID::Return", fields[9])


if __name__ == "__main__":
    unittest.main()

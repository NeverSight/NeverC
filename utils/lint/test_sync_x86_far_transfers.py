#!/usr/bin/env python3
"""Protect terminal scheduling and non-SP target-address carriers."""
import importlib.util
import unittest
from pathlib import Path
SPEC = importlib.util.spec_from_file_location(
    'far', Path(__file__).with_name('sync-x86-far-transfers.py'))
FAR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FAR)


class FarTransferTests(unittest.TestCase):
    def test_tables_match_and_keep_terminal_effects(self):
        for path, expected in FAR.synchronize().items():
            self.assertEqual(path.read_text(), expected, str(path))
        text = (FAR.TABLES.TARGET/'X86GenInstrInfo.inc').read_text()
        for name, kinds in FAR.SCHEMA:
            fields = FAR.TABLES.descriptor(text, name)
            for flag in ('Terminator', 'Barrier', 'UnmodeledSideEffects', 'MayLoad', 'MayStore'):
                self.assertIn(f'MCID::{flag}', fields[9], name)
            self.assertNotIn('MCID::Return', fields[9], name)
            self.assertNotIn('MCID::Call', fields[9], name)
            if 'IMM' not in name:
                self.assertTrue(kinds[0].endswith('_NOSP'), name)
        self.assertEqual(FAR.TABLES.enums(text)['NATIVE_ADDRESS64'], 18565)


if __name__ == '__main__': unittest.main()
